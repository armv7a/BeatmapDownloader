#include "pch.h"
#include "Utils.h"

#include <cctype>
#include <fstream>

namespace utils {
static HMODULE g_hModule = NULL;
static std::filesystem::path g_curDirPath;
static std::filesystem::path g_osuDirPath;

void SetMyModuleHandle(HMODULE hModule) {
    g_hModule = hModule;
}

HMODULE GetMyModuleHandle() {
    return g_hModule;
}

void SetCurrentDirPath(const std::filesystem::path &path) {
    g_curDirPath = path;
}

std::filesystem::path GetModulePath(HMODULE *hModule) {
    wchar_t pathOut[MAX_PATH] = {0};
    GetModuleFileNameW(*hModule, pathOut, MAX_PATH);
    return {pathOut};
}

void SetOsuDirPath(const std::filesystem::path &path) {
    g_osuDirPath = path;
}

std::filesystem::path GetCurrentDirPath() {
    return g_curDirPath;
}

std::filesystem::path GetOsuDirPath() {
    return g_osuDirPath;
}

bool IsOszArchive(const std::filesystem::path &path) {
    std::ifstream fs(path, std::ios::binary);
    if (!fs.is_open())
        return false;

    unsigned char magic[4]{};
    fs.read(reinterpret_cast<char *>(magic), sizeof(magic));
    if (fs.gcount() != sizeof(magic))
        return false;

    return magic[0] == 'P' && magic[1] == 'K' && (magic[2] == 0x03 || magic[2] == 0x05 || magic[2] == 0x07);
}

std::wstring s2ws(const std::string &s) {
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    wchar_t *buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}

std::string ws2s(const std::wstring &s) {
    int len;
    int slength = (int)s.length() + 1;
    len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, 0, 0, 0, 0);
    char *buf = new char[len];
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, buf, len, 0, 0);
    std::string r(buf);
    delete[] buf;
    return r;
}

std::string &trim_end(std::string &str) {
    str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), str.end());

    return str;
}

std::string &trim_begin(std::string &str) {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return str;
}

std::string &trim(std::string &str) {
    return trim_begin(trim_end(str));
}

std::string UrlEncode(const std::string &s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::vector<std::string> split(std::string res, const char split) {
    std::vector<std::string> result;
    if (res.empty())
        return result;

    std::string::size_type pos;
    while ((pos = res.find(split)) != std::string::npos) {
        auto r = res.substr(0, pos);
        result.push_back(r);
        res.erase(0, pos + 1);
    }

    result.push_back(res);
    return result;
}

std::vector<std::string> split(std::string res, const std::string &split) {
    std::vector<std::string> result;
    if (res.empty())
        return result;

    std::string::size_type pos;
    while ((pos = res.find(split)) != std::string::npos) {
        auto r = res.substr(0, pos);
        result.push_back(r);
        res.erase(0, pos + split.length());
    }

    result.push_back(res);
    return result;
}
}
