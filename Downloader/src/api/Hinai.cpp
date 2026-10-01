#include "pch.h"
#include "Hinai.h"
#include "network/HttpRequest.h"
#include "utils/Utils.h"
#include "api/chimu/Mapset.hpp"
#include "api/chimu/Map.hpp"

int32_t api::Hinai::_convertToSetId(const features::downloader::BeatmapInfo &info) const {
    if (info.type == features::downloader::BeatmapType::Sid) {
        return info.id;
    }

    const auto s = std::format("https://mirror.hinamizawa.ai/api/b/{0}", info.id);

    std::string response;
    int32_t resCode;
    if (const CURLcode code = net::curl_get(s.c_str(), response, &resCode); code != CURLE_OK || resCode != 200) {
        LOGW("Hinai search failed: code=%d %s", resCode, response.c_str());
        return -1;
    }

    const auto j = nlohmann::json::parse(response);
    api::chumi::ChumiMap map;
    api::chumi::from_json(j, map);
    if (map.error_message.has_value()) {
        LOGW("Hinai search failed: code=%d %s", resCode, map.error_message.value().c_str());
        return -1;
    }

    return static_cast<int32_t>(map.parent_set_id.value());
}

api::Hinai::Hinai() : Provider("Hinai", "https://mirror.hinamizawa.ai/docs", features::downloader::DownloadMirror::Hinai) {}

std::optional<osu::Beatmap> api::Hinai::searchBeatmap(const features::downloader::BeatmapInfo &info) const {
    int32_t id = _convertToSetId(info);
    if (id == -1) return {};

    const auto s = std::format("https://mirror.hinamizawa.ai/api/s/{0}", id);

    std::string response;
    int32_t resCode;
    if (const CURLcode code = net::curl_get(s.c_str(), response, &resCode); code != CURLE_OK || resCode != 200) {
        LOGW("Hinai search failed: code=%d %s", resCode, response.c_str());
        return {};
    }

    const auto j = nlohmann::json::parse(response);
    api::chumi::ChumiMapset mapset;
    api::chumi::from_json(j, mapset);

    if (mapset.error_code.has_value() || mapset.error_message.has_value()) {
        LOGW("Hinai search failed: code=%s %s", mapset.error_code.value().c_str(), mapset.error_message.value().c_str());
        return {};
    }

    return mapset.to_beatmap();
}

bool api::Hinai::downloadBeatmap(const osu::Beatmap &bm) const {
    auto &dl = features::Downloader::GetInstance();

    const auto s = std::format("https://mirror.hinamizawa.ai/api/d/{0}{1}", bm.sid,
                               dl.f_DownloadType.getValue() == features::downloader::DownloadType::NoVideo ? "n" : "");

    auto path = utils::GetCurrentDirPath() / L"downloads" / (std::to_string(bm.sid) + ".osz");
    auto *tsk = features::DownloadQueue::GetInstance().getTask(bm);

    int resCode = -1;
    if (const auto code = net::curl_download(s.c_str(), path, tsk, &resCode); code == CURLE_OK && resCode == 200) {
        LOGI("Success download beatmapsets: %d", bm.sid);
        return true;
    } else {
        LOGW("Hinai download failed: CURL_CODE=%d, RESPONSE_CODE=%d", code, resCode);
    }

    return false;
}
