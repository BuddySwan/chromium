// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/media_app_ui/media_app_ui.h"

#include "chromeos/components/media_app_ui/media_app_guest_ui.h"
#include "chromeos/components/media_app_ui/media_app_page_handler.h"
#include "chromeos/components/media_app_ui/url_constants.h"
#include "chromeos/grit/chromeos_media_app_bundle_resources.h"
#include "chromeos/grit/chromeos_media_app_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace chromeos {
namespace {

content::WebUIDataSource* CreateHostDataSource() {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(kChromeUIMediaAppHost);

  // Add resources from chromeos_media_app_resources.pak.
  source->SetDefaultResource(IDR_MEDIA_APP_INDEX_HTML);
  source->AddResourcePath("pwa.html", IDR_MEDIA_APP_PWA_HTML);
  source->AddResourcePath("manifest.json", IDR_MEDIA_APP_MANIFEST);
  source->AddResourcePath("launch.js", IDR_MEDIA_APP_LAUNCH_JS);
  source->AddResourcePath("browser_proxy.js", IDR_MEDIA_APP_BROWSER_PROXY_JS);
  source->AddResourcePath("media_app.mojom-lite.js",
                          IDR_MEDIA_APP_MEDIA_APP_MOJOM_JS);

  // Add resources from chromeos_media_app_bundle_resources.pak.
  source->AddResourcePath("system_assets/app_icon_256.png",
                          IDR_MEDIA_APP_APP_ICON_256_PNG);

  return source;
}

}  // namespace

MediaAppUI::MediaAppUI(content::WebUI* web_ui,
                       std::unique_ptr<MediaAppUIDelegate> delegate)
    : MojoWebUIController(web_ui), delegate_(std::move(delegate)) {
  content::BrowserContext* browser_context =
      web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource* host_source = CreateHostDataSource();
  content::WebUIDataSource::Add(browser_context, host_source);

  // Whilst the guest is in an <iframe> rather than a <webview>, we need a CSP
  // override to use the guest origin in the host.
  // TODO(crbug/996088): Remove these overrides when there's a new sandboxing
  // option for the guest.
  std::string csp = std::string("frame-src ") + kChromeUIMediaAppGuestURL + ";";
  host_source->OverrideContentSecurityPolicyChildSrc(csp);
}

MediaAppUI::~MediaAppUI() = default;

void MediaAppUI::BindInterface(
    mojo::PendingReceiver<media_app_ui::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MediaAppUI::CreatePageHandler(
    mojo::PendingRemote<media_app_ui::mojom::Page> page,
    mojo::PendingReceiver<media_app_ui::mojom::PageHandler> receiver) {
  DCHECK(page);
  page_handler_ = std::make_unique<MediaAppPageHandler>(this, std::move(page),
                                                        std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(MediaAppUI)

}  // namespace chromeos
