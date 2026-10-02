#include "pch.h"
#include "About.h"

#include "CustomHotkey.h"
#include "config/I18nManager.h"
#include "misc/Color.h"
#include "utils/gui_utils.h"
#include "dlver.h"

namespace features {

About::About() :
    Feature() {
}

FeatureInfo &About::getInfo() {
    static auto info = FeatureInfo{
        .category = "About",
        .groupName = ""
    };

    return info;
}

void About::drawMain() {
    auto &lang = i18n::I18nManager::GetInstance();
    ImGui::BeginGroupPanel(lang.getTextCStr("About"));
    ImGui::Text("%s", lang.getTextCStr("ProjectAuthor"));
    ImGui::Text("%s", lang.getTextCStr("ProjectLink"));
    ImGui::SameLine();
    ImGui::TextUrl("https://github.com/armv7a/BeatmapDownloader");
    ImGui::TextColored(color::Green, lang.getTextCStr("CurrentVersion"), DOWNLOADER_VERSION_STR, DOWNLOADER_COMMIT);
    ImGui::EndGroupPanel();

    ImGui::BeginGroupPanel(lang.getTextCStr("Hotkey"));
    auto &htk = features::CustomHotkey::GetInstance();
    ImGui::Text(lang.getTextCStr("HotkeyDesc"), htk.f_MainMenuHotkey->toString().c_str(), htk.f_SearchHotkey->toString().c_str());
    ImGui::EndGroupPanel();
}
}
