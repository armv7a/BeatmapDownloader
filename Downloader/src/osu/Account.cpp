#include "pch.h"
#include "Account.h"

#include "utils/MD5.hpp"

void osu::Password::to_json(nlohmann::json &j) const {
    j["PasswordHash"] = md5();
}

void osu::Password::from_json(const nlohmann::json &j) {
    if (const auto it = j.find("PasswordHash"); it != j.end() && it->is_string()) {
        m_Password = it->get<std::string>();
        m_StoredAsHash = true;
    }
}

std::string osu::Password::md5() const {
    if (m_Password.empty())
        return "";

    if (m_StoredAsHash)
        return m_Password;

    MD5 md5{};
    md5.update(m_Password);
    return md5.str();
}

void osu::Account::to_json(nlohmann::json &j) const {
    j["PasswordHash"] = m_Password.md5();
    j["Username"] = m_Username;
}

void osu::Account::from_json(const nlohmann::json &j) {
    if (const auto it = j.find("Username"); it != j.end() && it->is_string())
        m_Username = it->get<std::string>();
    m_Password.from_json(j);
}
