#pragma once

#include <cstdint>
#include <optional>
#include <regex>

#include "features/Downloader.h"

namespace osu {

class LinkParser {
public:
    static std::optional<features::downloader::BeatmapInfo> ParseLink(
        const std::string &link,
        const features::downloader::BeatmapType defIdType = features::downloader::BeatmapType::Sid) {

        static const std::regex sidRegex(R"((?:^|[/=])(?:beatmapsets|s)/(\d+))");
        static const std::regex bidRegex(R"((?:^|[/=])(?:beatmaps|b)/(\d+))");
        static const std::regex idRegex(R"(^(\d+)$)");

        std::smatch match;
        if (std::regex_search(link, match, sidRegex) && match.size() > 1) {
            if (const auto id = ParseId(match[1].str()); id.has_value()) {
                return features::downloader::BeatmapInfo{features::downloader::BeatmapType::Sid, *id};
            }
        }

        if (std::regex_search(link, match, bidRegex) && match.size() > 1) {
            if (const auto id = ParseId(match[1].str()); id.has_value()) {
                return features::downloader::BeatmapInfo{features::downloader::BeatmapType::Bid, *id};
            }
        }

        if (std::regex_search(link, match, idRegex) && match.size() > 1) {
            if (const auto id = ParseId(match[1].str()); id.has_value()) {
                return features::downloader::BeatmapInfo{defIdType, *id};
            }
        }

        return {};
    }

private:
    static std::optional<int> ParseId(const std::string &digits) {
        try {
            const auto id = std::stoll(digits);
            if (id <= 0 || id > INT32_MAX)
                return {};
            return static_cast<int>(id);
        } catch (...) {
            return {};
        }
    }
};

}
