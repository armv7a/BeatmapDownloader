#pragma once

#include <map>
#include <json.hpp>
#include "Field.h"
#include "misc/ResourcesLoader.hpp"
#include "../resource.h"

namespace i18n {
enum class Language : uint8_t {
    EN_US = 0,
    ZH_CN = 1,
};

class I18nManager {
    // i18nMap["en_us"]["Downloader"] -> "Downloader"
    inline static std::map<std::string, std::map<std::string, std::string>> i18nMap;

    static const char *GetLanguageName(Language lang) {
        switch (lang) {
        case Language::EN_US:
            return "EN_US";
        case Language::ZH_CN:
            return "ZH_CN";
        }

        return "EN_US";
    }

public:
    config::Field<Language> lang;

    static I18nManager &GetInstance() {
        static I18nManager instance;
        return instance;
    }

    static const std::string *Lookup(Language lang, std::string_view key) {
        const std::string k(key);
        if (const auto langIt = i18nMap.find(GetLanguageName(lang)); langIt != i18nMap.end()) {
            if (const auto it = langIt->second.find(k); it != langIt->second.end())
                return &it->second;
        }
        if (const auto enIt = i18nMap.find("EN_US"); enIt != i18nMap.end()) {
            if (const auto it = enIt->second.find(k); it != enIt->second.end())
                return &it->second;
        }
        return nullptr;
    }

    std::string_view getText(std::string_view key) {
        if (const auto *v = Lookup(lang, key); v != nullptr)
            return *v;
        return key;
    }

    static std::string_view GetText(std::string_view key) {
        return GetInstance().getText(key);
    }

    std::string_view getText(const char *key) {
        return getText(std::string_view(key));
    }

    static std::string_view GetText(const char *key) {
        return GetInstance().getText(key);
    }

    const char *getTextCStr(std::string_view key) {
        if (const auto *v = Lookup(lang, key); v != nullptr)
            return v->c_str();

        static thread_local std::string fallback;
        fallback.assign(key);
        return fallback.c_str();
    }

    static const char *GetTextCStr(std::string_view key) {
        return GetInstance().getTextCStr(key);
    }

    const char *getTextCStr(const char *key) {
        return getTextCStr(std::string_view(key));
    }

    static const char *GetTextCStr(const char *key) {
        return GetInstance().getTextCStr(key);
    }

private:
    I18nManager() :
        lang(config::Field("Language", Language::EN_US)) {
        BYTE *data = nullptr;

#define LOAD_LANG_FILE(lang) try { \
            if (const DWORD size = res::LoadEx(IDR_LANG_##lang, L"LANG", &data)) { \
                std::string s(reinterpret_cast<const char *>(data), size); \
                nlohmann::json j = nlohmann::json::parse(s); \
                for (auto &i : j.items()) i18nMap[#lang][i.key()] = i.value(); \
                LOGD("Loaded language file: %s, json size: %zu, map size: %zu", #lang, j.size(), i18nMap[#lang].size()); \
            } else { \
                LOGD("Cannot load language file: %s", #lang); \
            } \
        } catch (...) { \
            LOGE("Cannot initialize language file: %s", #lang); \
        }

        LOAD_LANG_FILE(EN_US)
        LOAD_LANG_FILE(ZH_CN)

#undef LOAD_LANG_FILE
    }
};
}
