#include "pch.h"
#include "Downloader.h"

#include <algorithm>
#include <curl.h>
#include <mutex>
#include <optional>
#include <set>
#include <shellapi.h>
#include <system_error>

#include "DownloadQueue.h"
#include "api/Provider.h"
#include "api/Sayobot.h"
#include "config/I18nManager.h"
#include "misc/Color.h"
#include "osu/BeatmapManager.h"
#include "ui/SearchResultUi.h"
#include "utils/gui_utils.h"

static std::set<int> s_Canceled{};
static std::mutex s_CanceledMutex{};
static std::mutex s_ThreadMutex{};
static std::atomic<int> s_DownloadThreadCount{0};
static std::atomic<uint64_t> s_LastSwitchToast{0};
static int s_EnsuredThreads = 0;

static bool ConsumeCanceled(const int sid) {
    std::lock_guard _g(s_CanceledMutex);
    return s_Canceled.erase(sid) > 0;
}

features::Downloader::Downloader() :
    Feature(),
    f_GrantOsuAccount("GrantOsuAccount", false),
    f_OsuAccount("OsuAccount", osu::Account{}),
    f_Mirror("DownloadMirror", downloader::DownloadMirror::Sayobot),
    f_DownloadType("DownloadType", downloader::DownloadType::Full),
    f_DownloadThreads("DownloadThreads", 3),
    f_MoveToOsuFolder("MoveToOsuFolder", false),
    f_ProxySeverType("ProxyServerType", downloader::ProxyServerType::Disabled),
    f_ProxySever("ProxyServer", "localhost:7890"),
    f_ProxySeverPassword("ProxyServerPassword", ""),
    f_EnableCustomUserAgent("EnableCustomUserAgent", false),
    f_CustomUserAgent("CustomUserAgent",
                      "BeatmapDownloader/1.0 (+https://github.com/armv7a/BeatmapDownloader)") {
    curl_global_init(CURL_GLOBAL_ALL);

    if (const int threads = std::clamp(f_DownloadThreads.getValue(), downloader::DOWNLOAD_THREADS_MIN,
                                       downloader::DOWNLOAD_THREADS_MAX);
        threads != f_DownloadThreads.getValue()) {
        f_DownloadThreads.setValue(threads);
    }
    ensureDownloadThreads(f_DownloadThreads.getValue());

    t_SearchThread = std::thread(SearchThread);
    t_SearchThread.detach();
    LOGI("Inited Downloader");
}

void features::Downloader::ensureDownloadThreads(const int want) {
    std::lock_guard _g(s_ThreadMutex);

    if (s_EnsuredThreads == want)
        return;

    while (s_DownloadThreadCount.load() < want) {
        s_DownloadThreadCount.fetch_add(1);
        std::thread(DownloadThread).detach();
    }

    for (int i = s_DownloadThreadCount.load(); i > want; --i) {
        downloader::DownloadJob job{};
        job.recycle = true;
        m_DownloadQueue.push_back(job);
    }

    s_EnsuredThreads = want;
    LOGD("Download threads: %d", want);
}

std::vector<features::downloader::DownloadMirror> features::Downloader::getMirrorCandidates() {
    const auto primary = f_Mirror.getValue();

    std::vector<downloader::DownloadMirror> ret{primary};
    for (const auto *provider : api::Provider::GetRegistered()) {
        const auto mirror = provider->getEnum();
        if (mirror == primary)
            continue;
        if (mirror == downloader::DownloadMirror::OsuOfficial && !f_GrantOsuAccount.getValue())
            continue;
        ret.push_back(mirror);
    }

    return ret;
}

void features::Downloader::DownloadThread() {
    auto &inst = GetInstance();
    auto &lang = i18n::I18nManager::GetInstance();

    while (true) {
        auto job = inst.m_DownloadQueue.pop_front();

        if (job.recycle) {
            const auto want = std::clamp(inst.f_DownloadThreads.getValue(), downloader::DOWNLOAD_THREADS_MIN,
                                         downloader::DOWNLOAD_THREADS_MAX);
            if (s_DownloadThreadCount.load() > want) {
                s_DownloadThreadCount.fetch_sub(1);
                LOGD("Recycled a download thread, left %d.", s_DownloadThreadCount.load());
                return;
            }
            continue;
        }

        auto &bm = job.bm;
        LOGD("Poped dl: %d", bm.sid);
        if (bm.sid <= 0) {
            LOGW("Invalid beatmapsets id: %d, skipped download.", bm.sid);
            continue;
        }
        if (ConsumeCanceled(bm.sid)) {
            LOGI("Canceled: %d, skipped download.", bm.sid);
            continue;
        }
        if (inst.f_Mirror.getValue() == downloader::DownloadMirror::OsuOfficial && !inst.f_GrantOsuAccount.getValue()) {
            LOGW("Permission denied on download beatmap by osu!(Official) mirror.");
            DownloadQueue::GetInstance().notifyFinished(bm.sid);
            continue;
        }

        auto path = utils::GetCurrentDirPath() / L"downloads" / (std::to_string(bm.sid) + ".osz");

        LOGI("Start download: %d %s-%s (%s)", bm.sid, bm.artist.c_str(), bm.title.c_str(), bm.author.c_str());
        GuiHelper::ShowInfoToast(lang.getTextCStr("StartDownload"), bm.sid);

        const auto mirrors = inst.getMirrorCandidates();
        auto lastMirror = mirrors.front();
        bool success = false;

        for (int round = 0; round < downloader::DOWNLOAD_RETRY_ROUNDS && !success; ++round) {
            for (const auto mirror : mirrors) {
                const auto *provider = api::Provider::GetRegisteredByEnum(mirror);
                if (provider == nullptr)
                    continue;

                if (mirror != lastMirror) {
                    lastMirror = mirror;
                    LOGW("Download failed, switching mirror: %s", provider->getName().data());

                    if (const auto now = GetTickCount64(); now - s_LastSwitchToast.load() > 3000) {
                        s_LastSwitchToast.store(now);
                        GuiHelper::ShowWarnToast(lang.getTextCStr("SwitchingMirror"), provider->getName().data());
                    }
                }

                if (provider->downloadBeatmap(bm)) {
                    success = true;
                    break;
                }
            }
        }

        DownloadQueue::GetInstance().notifyFinished(bm.sid);

        if (!success) {
            LOGW("Download failed (out of retry times): %d", bm.sid);
            GuiHelper::ShowWarnToast(lang.getTextCStr("DownloadFailedOORT"), bm.sid);

            std::error_code ec;
            remove(path, ec);
            continue;
        }

        if (!exists(path)) {
            LOGW("Cannot open file (file not exists): %s", path.string().c_str());
            continue;
        }

        if (inst.f_MoveToOsuFolder.getValue()) {
            auto song = utils::GetOsuDirPath() / "Songs";
            if (!exists(song))
                create_directories(song);
            song /= path.filename();

            if (exists(song))
                remove(song);

            MoveFileW(path.wstring().c_str(), song.wstring().c_str());
        } else {
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_HIDE);
        }

        osu::BeatmapManager::GetInstance().addBeatmap(bm);

        LOGD("Finished download beatmapsets: %d", bm.sid);
        GuiHelper::ShowSuccessToast(lang.getTextCStr("DownloadSuccess"), bm.sid);
    }
}

[[noreturn]] void features::Downloader::SearchThread() {
    auto &inst = GetInstance();
    auto &lang = i18n::I18nManager::GetInstance();

    while (true) {
        auto info = inst.m_SearchQueue.pop_front();
        LOGD("Poped search: %d", info.id);

        const auto mirrors = inst.getMirrorCandidates();
        std::optional<osu::Beatmap> found;

        for (const auto mirror : mirrors) {
            const auto *provider = api::Provider::GetRegisteredByEnum(mirror);
            if (provider == nullptr)
                continue;

            found = provider->searchBeatmap(info);
            if (found.has_value())
                break;

            LOGD("Search failed on mirror %s, trying another.", provider->getName().data());
        }

        if (found.has_value()) {
            auto &bm = *found;
            if (osu::BeatmapManager::GetInstance().hasBeatmap(bm) && info.directDownload) {
                LOGI("Already has beatmapsets: %d, skipped direct download.", bm.sid);
                GuiHelper::ShowInfoToast(lang.getTextCStr("ExistsBeatmapSkipAutoDownload"), bm.sid);
                continue;
            }

            info.directDownload ? inst.postDownload(bm) : ui::search::result::ShowSearchInfo(bm);
        } else {
            LOGW("No such map found on all mirrors. ID=%d, Type=%s", info.id,
                 info.type == downloader::BeatmapType::Sid ? "beatmapsets" : "beatmapid");
            GuiHelper::ShowWarnToast(lang.getTextCStr("SearchFailed"), info.type == downloader::BeatmapType::Sid ? "sid" : "bid", info.id);
        }
    }
}

void features::Downloader::drawMain() {
    auto &lang = i18n::I18nManager::GetInstance();
    ImGui::Checkbox(lang.getTextCStr("GrantOsuAccount"), f_GrantOsuAccount.getPtr());
    GuiHelper::ShowTooltip(lang.getTextCStr("GrantOsuAccountDesc"));

    if (f_GrantOsuAccount.getValue()) {
        static std::string un = f_OsuAccount->username();
        static std::string pw = f_OsuAccount->password().md5();
        ImGui::Indent();
        if (ImGui::InputText(lang.getTextCStr("Username"), &un)) {
            f_OsuAccount->setUsername(un);
        }
        if (ImGui::PasswordInputText(lang.getTextCStr("Password"), &pw)) {
            f_OsuAccount->setPassword(pw);
        }
        ImGui::Unindent();
    }

    ImGui::Checkbox(lang.GetTextCStr("MoveToOsuFolder"), f_MoveToOsuFolder.getPtr());
    GuiHelper::ShowTooltip(lang.GetTextCStr("MoveToOsuFolderDesc"));

#pragma region Mirror
    static const char *mirrorNames[] = {
        "Osu! (official)",
        "Sayobot",
        "Hinai",
    };
    static int mirrorIdx = (int)f_Mirror.getValue();
    if (ImGui::Combo(lang.getTextCStr("Mirror"), &mirrorIdx, mirrorNames, IM_ARRAYSIZE(mirrorNames))) {
        f_Mirror.setValue(static_cast<downloader::DownloadMirror>(mirrorIdx));
    }
    GuiHelper::ShowTooltip(lang.getTextCStr("MirrorDesc"));
#pragma endregion

#pragma region DownloadType
    static const char *downloadTypeNames[] = {
        "Full",
        "NoVideo",
    };
    static int downloadTypeIdx = (int)f_DownloadType.getValue();
    if (ImGui::Combo(lang.getTextCStr("DownloadType"), &downloadTypeIdx, downloadTypeNames, IM_ARRAYSIZE(downloadTypeNames))) {
        f_DownloadType.setValue(static_cast<downloader::DownloadType>(downloadTypeIdx));
    }
#pragma endregion

#pragma region DownloadThreads
    if (ImGui::SmallButton("-##downloadthreads") && f_DownloadThreads.getValue() > downloader::DOWNLOAD_THREADS_MIN) {
        f_DownloadThreads.setValue(f_DownloadThreads.getValue() - 1);
        ensureDownloadThreads(f_DownloadThreads.getValue());
    }
    ImGui::SameLine();
    ImGui::Text("%d", f_DownloadThreads.getValue());
    ImGui::SameLine();
    if (ImGui::SmallButton("+##downloadthreads") && f_DownloadThreads.getValue() < downloader::DOWNLOAD_THREADS_MAX) {
        f_DownloadThreads.setValue(f_DownloadThreads.getValue() + 1);
        ensureDownloadThreads(f_DownloadThreads.getValue());
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(lang.getTextCStr("DownloadThreads"));
    GuiHelper::ShowTooltip(lang.getTextCStr("DownloadThreadsDesc"));
#pragma endregion

    if (f_Mirror.getValue() == downloader::DownloadMirror::OsuOfficial && !f_GrantOsuAccount.getValue()) {
        ImGui::TextColored(color::Orange, "%s", lang.getTextCStr("NotGrantOsuAccountButUseOfficialWarn"));
    }

    static const char *proxyTypeNames[] = {
        "Disabled",
        "HTTP",
        "Socks5"
    };
    static int proxyTypeIdx = (int)f_ProxySeverType.getValue();
    if (ImGui::Combo(lang.getTextCStr("ProxyServerType"), &proxyTypeIdx, proxyTypeNames, IM_ARRAYSIZE(proxyTypeNames))) {
        f_ProxySeverType.setValue(static_cast<downloader::ProxyServerType>(proxyTypeIdx));
    };
    GuiHelper::ShowTooltip(lang.getTextCStr("ProxyServerDesc"));

    if (f_ProxySeverType.getValue() != downloader::ProxyServerType::Disabled) {
        ImGui::Indent();
        ImGui::InputText(lang.getTextCStr("ProxyServer"), f_ProxySever.getPtr());
        GuiHelper::ShowTooltip("e.g.: localhost:7890");
        ImGui::InputText(lang.getTextCStr("ProxyServerPassword"), f_ProxySeverPassword.getPtr());
        GuiHelper::ShowTooltip("e.g.: username:password");
        ImGui::Unindent();
    }

    ImGui::Checkbox(lang.getTextCStr("EnableCustomUserAgent"), f_EnableCustomUserAgent.getPtr());
    GuiHelper::ShowTooltip(lang.getTextCStr("CustomUserAgentDesc"));
    if (f_EnableCustomUserAgent.getValue()) {
        ImGui::Indent();
        ImGui::InputText("User-Agent", f_CustomUserAgent.getPtr());
        ImGui::Unindent();
    }
}

features::FeatureInfo &features::Downloader::getInfo() {
    static FeatureInfo info = {"Downloader", "Download"};
    return info;
}

void features::Downloader::CancelDownload(int sid) {
    std::lock_guard _g(s_CanceledMutex);
    s_Canceled.insert(sid);
}

void features::Downloader::RemoveCancelDownload(int sid) {
    std::lock_guard _g(s_CanceledMutex);
    s_Canceled.erase(sid);
}

void features::Downloader::postSearch(const downloader::BeatmapInfo &info) {
    m_SearchQueue.push_back(info);
}

void features::Downloader::postDownload(const osu::Beatmap &bm) {
    if (bm.sid <= 0) {
        LOGW("Invalid beatmapsets id: %d, skipped download.", bm.sid);
        return;
    }

    if (!DownloadQueue::GetInstance().addTask(bm))
        return;

    m_DownloadQueue.push_back(downloader::DownloadJob{.bm = bm});
}
