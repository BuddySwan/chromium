// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/new_tab_page/new_tab_page_handler.h"

#include "base/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/background/ntp_background_service.h"
#include "chrome/browser/search/background/ntp_background_service_factory.h"
#include "chrome/browser/search/chrome_colors/chrome_colors_factory.h"
#include "chrome/browser/search/chrome_colors/chrome_colors_service.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/common/search/generated_colors_info.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_utils.h"

namespace {

new_tab_page::mojom::ThemePtr MakeTheme(const NtpTheme& ntp_theme) {
  auto theme = new_tab_page::mojom::Theme::New();
  if (ntp_theme.using_default_theme) {
    theme->type = new_tab_page::mojom::ThemeType::DEFAULT;
    // TODO(crbug.com/1040682): This info has no meaning for the default theme
    // and shouldn't be used. We set it here to prevent a crash where mojo is
    // complaing about an unset info. However, we cannot make the field optional
    // as that is crashing JS. Once the JS crash is solved remove this line.
    theme->info = new_tab_page::mojom::ThemeInfo::NewChromeThemeId(-1);
  } else if (ntp_theme.color_id == -1) {
    theme->type = new_tab_page::mojom::ThemeType::THIRD_PARTY;
    auto info = new_tab_page::mojom::ThirdPartyThemeInfo::New();
    info->id = ntp_theme.theme_id;
    info->name = ntp_theme.theme_name;
    theme->info =
        new_tab_page::mojom::ThemeInfo::NewThirdPartyThemeInfo(std::move(info));
  } else if (ntp_theme.color_id == 0) {
    theme->type = new_tab_page::mojom::ThemeType::AUTOGENERATED;
    auto theme_colors = new_tab_page::mojom::ThemeColors::New();
    theme_colors->frame = ntp_theme.color_dark;
    theme_colors->active_tab = ntp_theme.color_light;
    theme->info = new_tab_page::mojom::ThemeInfo::NewAutogeneratedThemeColors(
        std::move(theme_colors));
  } else {
    theme->type = new_tab_page::mojom::ThemeType::CHROME;
    theme->info =
        new_tab_page::mojom::ThemeInfo::NewChromeThemeId(ntp_theme.color_id);
  }
  theme->background_color = ntp_theme.background_color;
  theme->shortcut_background_color = ntp_theme.shortcut_color;
  theme->shortcut_text_color = ntp_theme.text_color;
  theme->is_dark = !color_utils::IsDark(ntp_theme.text_color);
  return theme;
}

}  // namespace

NewTabPageHandler::NewTabPageHandler(
    mojo::PendingReceiver<new_tab_page::mojom::PageHandler>
        pending_page_handler,
    mojo::PendingRemote<new_tab_page::mojom::Page> pending_page,
    Profile* profile)
    : chrome_colors_service_(
          chrome_colors::ChromeColorsFactory::GetForProfile(profile)),
      instant_service_(InstantServiceFactory::GetForProfile(profile)),
      ntp_background_service_(
          NtpBackgroundServiceFactory::GetForProfile(profile)),
      page_{std::move(pending_page)},
      receiver_{this, std::move(pending_page_handler)} {
  CHECK(instant_service_);
  CHECK(ntp_background_service_);
  instant_service_->AddObserver(this);
  ntp_background_service_->AddObserver(this);
  page_->SetTheme(MakeTheme(*instant_service_->GetInitializedNtpTheme()));
}

NewTabPageHandler::~NewTabPageHandler() {
  instant_service_->RemoveObserver(this);
  ntp_background_service_->RemoveObserver(this);
}

void NewTabPageHandler::AddMostVisitedTile(
    const GURL& url,
    const std::string& title,
    AddMostVisitedTileCallback callback) {
  bool success = instant_service_->AddCustomLink(url, title);
  std::move(callback).Run(success);
}

void NewTabPageHandler::DeleteMostVisitedTile(const GURL& url) {
  if (instant_service_->IsCustomLinksEnabled()) {
    instant_service_->DeleteCustomLink(url);
  } else {
    instant_service_->DeleteMostVisitedItem(url);
    last_blacklisted_ = url;
  }
}

void NewTabPageHandler::RestoreMostVisitedDefaults() {
  if (instant_service_->IsCustomLinksEnabled()) {
    instant_service_->ResetCustomLinks();
  } else {
    instant_service_->UndoAllMostVisitedDeletions();
  }
}

void NewTabPageHandler::ReorderMostVisitedTile(const GURL& url,
                                               uint8_t new_pos) {
  instant_service_->ReorderCustomLink(url, new_pos);
}

void NewTabPageHandler::SetMostVisitedSettings(bool custom_links_enabled,
                                               bool visible) {
  auto pair = instant_service_->GetCurrentShortcutSettings();
  // The first of the pair is true if most-visited tiles are being used.
  bool old_custom_links_enabled = !pair.first;
  bool old_visible = pair.second;
  // |ToggleMostVisitedOrCustomLinks()| always notifies observers. Since we only
  // want to notify once, we need to call |ToggleShortcutsVisibility()| with
  // false if we are also going to call |ToggleMostVisitedOrCustomLinks()|.
  bool toggleCustomLinksEnabled =
      old_custom_links_enabled != custom_links_enabled;
  if (old_visible != visible) {
    instant_service_->ToggleShortcutsVisibility(
        /* do_notify= */ !toggleCustomLinksEnabled);
  }
  if (toggleCustomLinksEnabled) {
    instant_service_->ToggleMostVisitedOrCustomLinks();
  }
}

void NewTabPageHandler::UndoMostVisitedTileAction() {
  if (instant_service_->IsCustomLinksEnabled()) {
    instant_service_->UndoCustomLinkAction();
  } else if (last_blacklisted_.is_valid()) {
    instant_service_->UndoMostVisitedDeletion(last_blacklisted_);
    last_blacklisted_ = GURL();
  }
}

void NewTabPageHandler::GetChromeThemes(GetChromeThemesCallback callback) {
  std::vector<new_tab_page::mojom::ChromeThemePtr> themes;
  for (const auto& color_info : chrome_colors::kGeneratedColorsInfo) {
    auto theme_colors = GetAutogeneratedThemeColors(color_info.color);
    auto theme = new_tab_page::mojom::ChromeTheme::New();
    theme->id = color_info.id;
    theme->label = l10n_util::GetStringUTF8(color_info.label_id);
    auto colors = new_tab_page::mojom::ThemeColors::New();
    colors->frame = theme_colors.frame_color;
    colors->active_tab = theme_colors.active_tab_color;
    theme->colors = std::move(colors);
    themes.push_back(std::move(theme));
  }
  std::move(callback).Run(std::move(themes));
}

void NewTabPageHandler::ApplyDefaultTheme() {
  chrome_colors_service_->ApplyDefaultTheme(web_contents());
}

void NewTabPageHandler::ApplyAutogeneratedTheme(const SkColor& frame_color) {
  chrome_colors_service_->ApplyAutogeneratedTheme(frame_color, web_contents());
}

void NewTabPageHandler::ApplyChromeTheme(int32_t id) {
  auto* begin = std::begin(chrome_colors::kGeneratedColorsInfo);
  auto* end = std::end(chrome_colors::kGeneratedColorsInfo);
  auto* result = std::find_if(begin, end,
                              [id](const chrome_colors::ColorInfo& color_info) {
                                return color_info.id == id;
                              });
  if (result == end) {
    return;
  }
  chrome_colors_service_->ApplyAutogeneratedTheme(result->color,
                                                  web_contents());
}

void NewTabPageHandler::ConfirmThemeChanges() {
  chrome_colors_service_->ConfirmThemeChanges();
}

void NewTabPageHandler::RevertThemeChanges() {
  chrome_colors_service_->RevertThemeChanges();
}

void NewTabPageHandler::UpdateMostVisitedInfo() {
  instant_service_->UpdateMostVisitedInfo();
}

void NewTabPageHandler::UpdateMostVisitedTile(
    const GURL& url,
    const GURL& new_url,
    const std::string& new_title,
    UpdateMostVisitedTileCallback callback) {
  bool success = instant_service_->UpdateCustomLink(
      url, new_url != url ? new_url : GURL(), new_title);
  std::move(callback).Run(success);
}

void NewTabPageHandler::GetBackgroundCollections(
    GetBackgroundCollectionsCallback callback) {
  if (!ntp_background_service_) {
    std::move(callback).Run(
        std::vector<new_tab_page::mojom::BackgroundCollectionPtr>());
    return;
  }
  background_collections_callback_ = std::move(callback);
  ntp_background_service_->FetchCollectionInfo();
}

void NewTabPageHandler::NtpThemeChanged(const NtpTheme& ntp_theme) {
  page_->SetTheme(MakeTheme(ntp_theme));
}

void NewTabPageHandler::MostVisitedInfoChanged(
    const InstantMostVisitedInfo& info) {
  std::vector<new_tab_page::mojom::MostVisitedTilePtr> list;
  auto result = new_tab_page::mojom::MostVisitedInfo::New();
  for (auto& tile : info.items) {
    auto value = new_tab_page::mojom::MostVisitedTile::New();
    if (tile.title.empty()) {
      value->title = tile.url.spec();
      value->title_direction = base::i18n::LEFT_TO_RIGHT;
    } else {
      value->title = base::UTF16ToUTF8(tile.title);
      value->title_direction =
          base::i18n::GetFirstStrongCharacterDirection(tile.title);
    }
    value->url = tile.url;
    list.push_back(std::move(value));
  }
  result->custom_links_enabled = !info.use_most_visited;
  result->tiles = std::move(list);
  result->visible = info.is_visible;
  page_->SetMostVisitedInfo(std::move(result));
}

void NewTabPageHandler::OnCollectionInfoAvailable() {
  if (!background_collections_callback_) {
    return;
  }

  std::vector<new_tab_page::mojom::BackgroundCollectionPtr> collections;
  for (const auto& info : ntp_background_service_->collection_info()) {
    auto collection = new_tab_page::mojom::BackgroundCollection::New();
    collection->label = info.collection_name;
    collection->preview_image_url = GURL(info.preview_image_url);
    collections.push_back(std::move(collection));
  }
  std::move(background_collections_callback_).Run(std::move(collections));
}

void NewTabPageHandler::OnCollectionImagesAvailable() {}

void NewTabPageHandler::OnNextCollectionImageAvailable() {}

void NewTabPageHandler::OnNtpBackgroundServiceShuttingDown() {
  ntp_background_service_->RemoveObserver(this);
  ntp_background_service_ = nullptr;
}
