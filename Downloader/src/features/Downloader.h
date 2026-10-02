#pragma once
#include "config/Field.h"
#include "Feature.h"
#include <BlockingContainer.hpp>
#include <atomic>
#include <deque>
#include <thread>
#include <vector>

#include "osu/Account.h"
#include "osu/Beatmap.h"

namespace features {
namespace downloader {

enum class DownloadMirror : uint8_t {
    OsuOfficial = 0,
    Sayobot,
    Hinai,
};

enum class BeatmapType : uint8_t {
    Sid = 0,
    Bid,
};

enum class DownloadType : uint8_t {
    Full = 0,
    NoVideo
};

struct BeatmapInfo {
    BeatmapType type;
    int32_t id;
    bool directDownload = false;
};

struct DownloadInfo {
    DownloadType type;
    int32_t id;
};

enum class ProxyServerType : uint8_t {
    Disabled = 0,
    Http,
    Socks5,
};

struct DownloadJob {
    osu::Beatmap bm;
    bool recycle = false;
};

constexpr int DOWNLOAD_THREADS_MIN = 1;
constexpr int DOWNLOAD_THREADS_MAX = 16;
constexpr int DOWNLOAD_RETRY_ROUNDS = 3;

}


class Downloader : public Feature {
    Downloader();

    container::Blocking<std::deque<downloader::DownloadJob>> m_DownloadQueue;
    container::Blocking<std::deque<downloader::BeatmapInfo>> m_SearchQueue;

    static void DownloadThread();
    [[noreturn]] static void SearchThread();

    std::thread t_SearchThread;

    void ensureDownloadThreads(int want);
    [[nodiscard]] std::vector<downloader::DownloadMirror> getMirrorCandidates();

public:
    inline const static char *DEFAULT_USER_AGENT =
        "BeatmapDownloader/1.0 (+https://github.com/armv7a/BeatmapDownloader)";

    config::Field<bool> f_GrantOsuAccount;
    config::Field<osu::Account> f_OsuAccount;
    config::Field<downloader::DownloadMirror> f_Mirror;
    config::Field<downloader::DownloadType> f_DownloadType;
    config::Field<int> f_DownloadThreads;
    config::Field<bool> f_MoveToOsuFolder;

    config::Field<downloader::ProxyServerType> f_ProxySeverType;
    config::Field<std::string> f_ProxySever;
    config::Field<std::string> f_ProxySeverPassword;

    config::Field<bool> f_EnableCustomUserAgent;
    config::Field<std::string> f_CustomUserAgent;


    static Downloader &GetInstance() {
        static Downloader instance;
        return instance;
    }

    void drawMain() override;
    FeatureInfo &getInfo() override;

    static void CancelDownload(int sid);
    static void RemoveCancelDownload(int sid);

    void postSearch(const downloader::BeatmapInfo &info);
    void postDownload(const osu::Beatmap &bm);
};
}
