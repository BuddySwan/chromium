// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/ssl_error_controller_client.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/launch.h"
#include "base/task/post_task.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/interstitials/enterprise_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_ssl_host_state_delegate_factory.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/security_interstitials/content/chrome_ssl_host_state_delegate.h"
#include "components/security_interstitials/content/content_metrics_helper.h"
#include "components/security_interstitials/content/utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/common/webui_url_constants.h"
#endif

#if defined(OS_WIN)
#include "base/base_paths_win.h"
#include "base/path_service.h"
#include "base/strings/string16.h"
#include "base/win/windows_version.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

using content::Referrer;

namespace {

void RecordRecurrentErrorAction(
    SSLErrorControllerClient::RecurrentErrorAction action,
    int cert_error) {
  UMA_HISTOGRAM_ENUMERATION("interstitial.ssl_recurrent_error.action", action);
  if (cert_error == net::ERR_CERTIFICATE_TRANSPARENCY_REQUIRED) {
    UMA_HISTOGRAM_ENUMERATION(
        "interstitial.ssl_recurrent_error.ct_error.action", action);
  }
}

bool HasSeenRecurrentErrorInternal(content::WebContents* web_contents,
                                   int cert_error) {
  ChromeSSLHostStateDelegate* state =
      ChromeSSLHostStateDelegateFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()));
  return state->HasSeenRecurrentErrors(cert_error);
}

}  // namespace

SSLErrorControllerClient::SSLErrorControllerClient(
    content::WebContents* web_contents,
    const net::SSLInfo& ssl_info,
    int cert_error,
    const GURL& request_url,
    std::unique_ptr<security_interstitials::MetricsHelper> metrics_helper)
    : SecurityInterstitialControllerClient(
          web_contents,
          std::move(metrics_helper),
          Profile::FromBrowserContext(web_contents->GetBrowserContext())
              ->GetPrefs(),
          g_browser_process->GetApplicationLocale(),
          GURL(chrome::kChromeUINewTabURL)),
      ssl_info_(ssl_info),
      request_url_(request_url),
      cert_error_(cert_error) {
  if (HasSeenRecurrentErrorInternal(web_contents_, cert_error_)) {
    RecordRecurrentErrorAction(RecurrentErrorAction::kShow, cert_error_);
  }
}

SSLErrorControllerClient::~SSLErrorControllerClient() {}

void SSLErrorControllerClient::GoBack() {
  SecurityInterstitialControllerClient::GoBackAfterNavigationCommitted();
}

void SSLErrorControllerClient::Proceed() {
  if (HasSeenRecurrentErrorInternal(web_contents_, cert_error_)) {
    RecordRecurrentErrorAction(RecurrentErrorAction::kProceed, cert_error_);
  }

  MaybeTriggerSecurityInterstitialProceededEvent(web_contents_, request_url_,
                                                 "SSL_ERROR", cert_error_);
#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Hosted Apps should not be allowed to run if there is a problem with their
  // certificate. So, when users click proceed on an interstitial, move the tab
  // to a regular Chrome window and proceed as usual there.
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents_);
  if (web_app::AppBrowserController::IsForWebAppBrowser(browser))
    chrome::OpenInChrome(browser);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  ChromeSSLHostStateDelegate* state = static_cast<ChromeSSLHostStateDelegate*>(
      profile->GetSSLHostStateDelegate());
  // ChromeSSLHostStateDelegate can be null during tests.
  if (state) {
    state->AllowCert(request_url_.host(), *ssl_info_.cert.get(), cert_error_,
                     web_contents_);
    Reload();
  }
}

bool SSLErrorControllerClient::CanLaunchDateAndTimeSettings() {
#if defined(OS_ANDROID) || defined(OS_LINUX) || defined(OS_MACOSX) || \
    defined(OS_WIN)
  return true;
#else
  return false;
#endif
}

void SSLErrorControllerClient::LaunchDateAndTimeSettings() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

#if defined(OS_CHROMEOS)
  chrome::SettingsWindowManager::GetInstance()->ShowOSSettings(
      ProfileManager::GetActiveUserProfile(), chrome::kDateTimeSubPage);
#else
  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&security_interstitials::LaunchDateAndTimeSettings));
#endif
}

bool SSLErrorControllerClient::HasSeenRecurrentError() {
  return HasSeenRecurrentErrorInternal(web_contents_, cert_error_);
}
