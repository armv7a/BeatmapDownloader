#include "pch.h"
#include <fstream>
#include <utility>

#include <shellapi.h>
#include "BeatmapManager.h"

#include "features/HandleLinkHook.h"
#include "utils/Utils.h"

osu::BeatmapManager::BeatmapManager() {
    initDatabase();
}

// taken from https://github.com/veritas501/Osu-Ingame-Downloader/blob/master/OsuIngameDownloader/map_db.cpp#L15
#define fr(obj) fs.read((char*) &(obj), sizeof(obj))
#define fp(size) fs.seekg(size, std::ios::cur)

unsigned int UnpackULEB128(std::ifstream &fs) {
    char flag = 0;
    fr(flag);
    if (flag != 0xb) {
        return 0;
    }
    unsigned char tmpByte = 0;
    unsigned int decodeNum = 0;
    int i = 0;
    do {
        fr(tmpByte);
        decodeNum += (tmpByte & 0x7f) << 7 * i;
        i++;
    } while (tmpByte >= 0x80);
    return decodeNum;
}

std::string UnpackOsuStr(std::ifstream &fs) {
    const uint32_t size = UnpackULEB128(fs);
    if (!size)
        return "";

    char *tmpStr = new char[size + 1];
    fs.read(tmpStr, size);
    tmpStr[size] = 0;

    std::string ans(tmpStr);
    delete[] tmpStr;
    return ans;
}

void PassOsuStr(std::ifstream &fs) {
    const uint32_t size = UnpackULEB128(fs);
    if (!size)
        return;
    fp(size);
}

namespace {
int getOsudbVersion(const std::filesystem::path &dbPath) {
    std::ifstream fs(dbPath, std::ios::binary | std::ios::in);
    if (!fs.is_open())
        return 0;

    int version = 0;
    fs.read(reinterpret_cast<char *>(&version), sizeof(version));
    return fs ? version : 0;
}

bool parseOsudb(const std::filesystem::path &dbPath, int starPairSize,
                std::set<int32_t> &outBids, std::set<int32_t> &outSids) {
    std::ifstream fs;
    int bid = 0, sid = 0;

    fs.open(dbPath, std::ios::binary | std::ios::in);
    if (!fs.is_open())
        return false;

    fs.seekg(0, std::ios::end);
    const std::streamoff fileSize = fs.tellg();
    fs.seekg(0, std::ios::beg);

    fp(0x11);
    PassOsuStr(fs);
    int sumBeatmaps = 0;
    fr(sumBeatmaps);

    if (!fs || sumBeatmaps < 0 || sumBeatmaps > 1000000) {
        LOGE("osu!.db header looks invalid (beatmap count = %d)", sumBeatmaps);
        return false;
    }

    std::set<int32_t> bids, sids;
    for (int i = 0; i < sumBeatmaps; i++) {
        for (int j = 0; j < 9; j++) {
            PassOsuStr(fs);
        }
        fp(1 + 2 * 3 + 8 + 4 * 4 + 8);
        for (int j = 0; j < 4; j++) {
            int length = 0;
            fr(length);
            if (length < 0 || length > 10000) {
                fs.setstate(std::ios::failbit);
                break;
            }
            fp(length * starPairSize);
        }
        if (!fs)
            break;

        fp(4 * 3);
        int timingPointsLength = 0;
        fr(timingPointsLength);
        if (timingPointsLength < 0 || timingPointsLength > 1000000) {
            fs.setstate(std::ios::failbit);
            break;
        }
        fp(timingPointsLength * (8 * 2 + 1));

        fr(bid);
        fr(sid);
        fp(4 + 1 * 4 + 2 + 4 + 1);
        PassOsuStr(fs);
        PassOsuStr(fs);
        fp(2);
        PassOsuStr(fs);
        fp(1 + 8 + 1);
        PassOsuStr(fs);
        fp(8 + 1 * 5 + 4 + 1);
        if (!fs)
            break;

        if (bid != -1 && bid != 0) {
            bids.insert(bid);
        }
        if (sid != -1 && sid != 0) {
            sids.insert(sid);
        }
    }

    const std::streamoff pos = fs.tellg();
    if (!fs || pos < fileSize - 4096 || pos > fileSize + 4096)
        return false;

    outBids = std::move(bids);
    outSids = std::move(sids);
    return true;
}
} // namespace

void osu::BeatmapManager::initDatabase() {
    const auto osuDatabase = utils::GetOsuDirPath() / "osu!.db";
    if (!exists(osuDatabase)) {
        LOGW("osu!.db not found!");
        return;
    }

    LOGI("Start initialize osu! database...");

    const int preferredPairSize = getOsudbVersion(osuDatabase) >= 20260000 ? 10 : 14;

    std::set<int32_t> bids, sids;
    if (!parseOsudb(osuDatabase, preferredPairSize, bids, sids)) {
        const int fallbackPairSize = preferredPairSize == 10 ? 14 : 10;
        LOGW("osu!.db layout mismatch, retrying with %d-byte star pairs...", fallbackPairSize);
        if (!parseOsudb(osuDatabase, fallbackPairSize, bids, sids)) {
            LOGE("Failed to parse osu!.db (unsupported or corrupted database).");
            return;
        }
    }

    {
        std::unique_lock _g(m_rwlock);
        m_BeatmapIds = std::move(bids);
        m_BeatmapSetIds = std::move(sids);
    }

    LOGI("Finished initialize osu! database! sid size = %zu, bid size = %zu", m_BeatmapSetIds.size(), m_BeatmapIds.size());
}

void osu::BeatmapManager::openBeatmapPage(Beatmap &bm) {
    if (bm.sid <= 0) {
        LOGW("Cannot open website: Invalid beatmap id %d!", bm.sid);
        return;
    }
    std::wstring s = L"https://osu.ppy.sh/s/";
    s.append(std::to_wstring(bm.sid));
    WLOGI(L"Opening website: %s", s.c_str());
    
    SHELLEXECUTEINFOW sei = {
        sizeof(sei),
        0,
        nullptr,
        L"open",
        s.c_str(),
        nullptr,
        nullptr,
        SW_HIDE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr
    };

    // prevent trigger search
    HookManager::CallOriginal(features::HandleLinkHook::ShellExecuteExW_Hook, &sei);
}

#undef fr
#undef fp
