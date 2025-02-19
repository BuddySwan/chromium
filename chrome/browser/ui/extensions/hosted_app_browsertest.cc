// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_forward.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/user_action_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/predictors/loading_predictor_config.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/ssl_test_utils.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/ui/web_applications/web_app_launch_utils.h"
#include "chrome/browser/ui/web_applications/web_app_menu_model.h"
#include "chrome/browser/web_applications/components/app_shortcut_manager.h"
#include "chrome/browser/web_applications/components/external_install_options.h"
#include "chrome/browser/web_applications/components/web_app_id.h"
#include "chrome/browser/web_applications/components/web_app_provider_base.h"
#include "chrome/browser/web_applications/test/web_app_install_observer.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/web_application_info.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/sessions/core/tab_restore_service.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/test/test_extension_dir.h"
#include "net/base/host_port_pair.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/request_handler_util.h"
#include "testing/gtest/include/gtest/gtest-param-test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"

using content::RenderFrameHost;
using content::WebContents;
using extensions::Extension;
using web_app::GetAppMenuCommandState;
using web_app::IsBrowserOpen;
using web_app::kDisabled;
using web_app::kEnabled;
using web_app::kNotPresent;
using web_app::NavigateAndCheckForToolbar;
using web_app::NavigateToURLAndWait;

namespace {

constexpr const char kImagePath[] = "/ssl/google_files/logo.gif";
constexpr const char kAppDotComManifest[] =
    "{"
    "  \"name\": \"Hosted App\","
    "  \"version\": \"1\","
    "  \"manifest_version\": 2,"
    "  \"app\": {"
    "    \"launch\": {"
    "      \"web_url\": \"%s\""
    "    },"
    "    \"urls\": [\"*://app.com/\"]"
    "  }"
    "}";

enum class AppType {
  HOSTED_APP,    // Using HostedAppBrowserController
  BOOKMARK_APP,  // Using HostedAppBrowserController
};

void CheckWebContentsHasAppPrefs(content::WebContents* web_contents) {
  blink::mojom::RendererPreferences* prefs =
      web_contents->GetMutableRendererPrefs();
  EXPECT_FALSE(prefs->can_accept_load_drops);
}

void CheckWebContentsDoesNotHaveAppPrefs(content::WebContents* web_contents) {
  blink::mojom::RendererPreferences* prefs =
      web_contents->GetMutableRendererPrefs();
  EXPECT_TRUE(prefs->can_accept_load_drops);
}

// Tries to load an image at |image_url| and returns whether or not it loaded
// successfully.
//
// The image could fail to load because it was blocked from being loaded or
// because |image_url| doesn't exist. Therefore, it failing to load is not a
// reliable indicator of insecure content being blocked. Users of the function
// should check the state of security indicators.
bool TryToLoadImage(const content::ToRenderFrameHost& adapter,
                    const GURL& image_url) {
  const std::string script = base::StringPrintf(
      "let i = document.createElement('img');"
      "document.body.appendChild(i);"
      "i.addEventListener('load', () => domAutomationController.send(true));"
      "i.addEventListener('error', () => domAutomationController.send(false));"
      "i.src = '%s';",
      image_url.spec().c_str());

  bool image_loaded;
  CHECK(content::ExecuteScriptAndExtractBool(adapter, script, &image_loaded));
  return image_loaded;
}

}  // namespace

// Parameters are {app_type, desktop_pwa_flag}. |app_type| controls whether it
// is a Hosted or Bookmark app.
class HostedOrBookmarkAppTest : public extensions::ExtensionBrowserTest,
                                public ::testing::WithParamInterface<AppType> {
 public:
  HostedOrBookmarkAppTest()
      : app_browser_(nullptr),
        app_(nullptr),
        https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    scoped_feature_list_.InitWithFeatures(
        {}, {features::kDesktopPWAsUnifiedUiController,
             predictors::kSpeculativePreconnectFeature});
  }
  ~HostedOrBookmarkAppTest() override {}

  void SetUp() override {
    https_server_.AddDefaultHandlers(GetChromeTestDataDir());

    app_type_ = GetParam();

    extensions::ExtensionBrowserTest::SetUp();
  }

 protected:
  void SetupAppWithURL(const GURL& app_url) {
    // TODO(ortuno): Use InstallBookmarkApp instead of loading a manifest,
    // if |app_type_ == BOOKMARK_APP|.
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(
        base::StringPrintf(kAppDotComManifest, app_url.spec().c_str()));
    SetupApp(test_app_dir.UnpackedPath());
  }

  void SetupApp(const std::string& app_folder) {
    SetupApp(test_data_dir_.AppendASCII(app_folder));
  }

  void SetupApp(const base::FilePath& app_folder) {
    app_ = InstallExtensionWithSourceAndFlags(
        app_folder, 1, extensions::Manifest::INTERNAL,
        app_type_ == AppType::HOSTED_APP
            ? extensions::Extension::NO_FLAGS
            : extensions::Extension::FROM_BOOKMARK);
    ASSERT_TRUE(app_);

    LaunchApp();
  }

  void LaunchApp() {
    // Launch app in a window.
    app_browser_ = LaunchAppBrowser(app_);
    ASSERT_TRUE(app_browser_);
    ASSERT_TRUE(app_browser_ != browser());
  }

  GURL GetMixedContentAppURL() {
    return https_server()->GetURL("app.com",
                                  "/ssl/page_displays_insecure_content.html");
  }

  GURL GetSecureAppURL() {
    return https_server()->GetURL("app.com", "/ssl/google.html");
  }

  GURL GetInstallableAppURL() {
    return https_server()->GetURL("/banners/manifest_test_page.html");
  }

  static const char* GetInstallableAppName() { return "Manifest test app"; }

  void SetUpInProcessBrowserTestFixture() override {
    extensions::ExtensionBrowserTest::SetUpInProcessBrowserTestFixture();
    cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    extensions::ExtensionBrowserTest::TearDownInProcessBrowserTestFixture();
    cert_verifier_.TearDownInProcessBrowserTestFixture();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    extensions::ExtensionBrowserTest::SetUpCommandLine(command_line);
    // Browser will both run and display insecure content.
    command_line->AppendSwitch(switches::kAllowRunningInsecureContent);
    cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    extensions::ExtensionBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    // By default, all SSL cert checks are valid. Can be overridden in tests.
    cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);

    web_app::WebAppProviderBase::GetProviderBase(profile())
        ->shortcut_manager()
        .SuppressShortcutsForTesting();
  }

  // Tests that performing |action| results in a new foreground tab
  // that navigated to |target_url| in the main browser window.
  void TestAppActionOpensForegroundTab(base::OnceClosure action,
                                       const GURL& target_url) {
    ASSERT_EQ(app_browser_, chrome::FindLastActive());

    size_t num_browsers = chrome::GetBrowserCount(profile());
    int num_tabs = browser()->tab_strip_model()->count();
    content::WebContents* initial_tab =
        browser()->tab_strip_model()->GetActiveWebContents();

    ASSERT_NO_FATAL_FAILURE(std::move(action).Run());

    EXPECT_EQ(num_browsers, chrome::GetBrowserCount(profile()));
    EXPECT_EQ(browser(), chrome::FindLastActive());
    EXPECT_EQ(++num_tabs, browser()->tab_strip_model()->count());

    content::WebContents* new_tab =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_NE(initial_tab, new_tab);
    EXPECT_EQ(target_url, new_tab->GetLastCommittedURL());
  }

  web_app::AppRegistrar& registrar() {
    auto* provider = web_app::WebAppProviderBase::GetProviderBase(profile());
    CHECK(provider);
    return provider->registrar();
  }

  Browser* app_browser_;
  const extensions::Extension* app_;

  AppType app_type() const { return app_type_; }

  net::EmbeddedTestServer* https_server() { return &https_server_; }

  content::ContentMockCertVerifier::CertVerifier* cert_verifier() {
    return cert_verifier_.mock_cert_verifier();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  AppType app_type_;

  net::EmbeddedTestServer https_server_;
  // Similar to net::MockCertVerifier, but also updates the CertVerifier
  // used by the NetworkService.
  content::ContentMockCertVerifier cert_verifier_;

  DISALLOW_COPY_AND_ASSIGN(HostedOrBookmarkAppTest);
};

// Tests that "Open link in new tab" opens a link in a foreground tab.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest, OpenLinkInNewTab) {
  SetupApp("app");

  const GURL url("http://www.foo.com/");
  TestAppActionOpensForegroundTab(
      base::BindOnce(
          [](content::WebContents* app_contents, const GURL& target_url) {
            ui_test_utils::UrlLoadObserver url_observer(
                target_url, content::NotificationService::AllSources());
            content::ContextMenuParams params;
            params.page_url = app_contents->GetLastCommittedURL();
            params.link_url = target_url;

            TestRenderViewContextMenu menu(app_contents->GetMainFrame(),
                                           params);
            menu.Init();
            menu.ExecuteCommand(IDC_CONTENT_CONTEXT_OPENLINKNEWTAB,
                                0 /* event_flags */);
            url_observer.Wait();
          },
          app_browser_->tab_strip_model()->GetActiveWebContents(), url),
      url);
}

// Tests that Ctrl + Clicking a link opens a foreground tab.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest, CtrlClickLink) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Set up an app which covers app.com URLs.
  GURL app_url =
      embedded_test_server()->GetURL("app.com", "/click_modifier/href.html");
  ui_test_utils::UrlLoadObserver url_observer(
      app_url, content::NotificationService::AllSources());
  SetupAppWithURL(app_url);
  // Wait for the URL to load so that we can click on the page.
  url_observer.Wait();

  const GURL url = embedded_test_server()->GetURL(
      "app.com", "/click_modifier/new_window.html");
  TestAppActionOpensForegroundTab(
      base::BindOnce(
          [](content::WebContents* app_contents, const GURL& target_url) {
            ui_test_utils::UrlLoadObserver url_observer(
                target_url, content::NotificationService::AllSources());
            int ctrl_key;
#if defined(OS_MACOSX)
            ctrl_key = blink::WebInputEvent::Modifiers::kMetaKey;
#else
            ctrl_key = blink::WebInputEvent::Modifiers::kControlKey;
#endif
            content::SimulateMouseClick(app_contents, ctrl_key,
                                        blink::WebMouseEvent::Button::kLeft);
            url_observer.Wait();
          },
          app_browser_->tab_strip_model()->GetActiveWebContents(), url),
      url);
}

// Tests that the WebContents of an app window launched using OpenApplication
// has the correct prefs.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       WebContentsPrefsOpenApplication) {
  SetupApp("https_app");
  CheckWebContentsHasAppPrefs(
      app_browser_->tab_strip_model()->GetActiveWebContents());
}

// Tests that the WebContents of an app window launched using
// web_app::ReparentWebContentsIntoAppBrowser has the correct prefs.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       WebContentsPrefsReparentWebContents) {
  SetupApp("https_app");

  content::WebContents* current_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  CheckWebContentsDoesNotHaveAppPrefs(current_tab);

  Browser* app_browser =
      web_app::ReparentWebContentsIntoAppBrowser(current_tab, app_->id());
  ASSERT_NE(browser(), app_browser);

  CheckWebContentsHasAppPrefs(
      chrome::FindLastActive()->tab_strip_model()->GetActiveWebContents());
}

// Tests that the WebContents of a regular browser window launched using
// OpenInChrome has the correct prefs.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest, WebContentsPrefsOpenInChrome) {
  SetupApp("https_app");

  content::WebContents* app_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  CheckWebContentsHasAppPrefs(app_contents);

  chrome::OpenInChrome(app_browser_);
  ASSERT_EQ(browser(), chrome::FindLastActive());

  CheckWebContentsDoesNotHaveAppPrefs(
      browser()->tab_strip_model()->GetActiveWebContents());
}

// Check that the toolbar is shown correctly.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest, ShouldShowCustomTabBar) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");

  SetupAppWithURL(app_url);

  // Navigate to the app's launch page; the toolbar should be hidden.
  NavigateAndCheckForToolbar(app_browser_, app_url, false);

  // Navigate to another page on the same origin; the toolbar should still
  // hidden.
  NavigateAndCheckForToolbar(
      app_browser_, https_server()->GetURL("app.com", "/empty.html"), false);

  // Navigate to different origin; the toolbar should now be visible.
  NavigateAndCheckForToolbar(
      app_browser_, https_server()->GetURL("foo.com", "/simple.html"), true);
}

using HostedAppTest = HostedOrBookmarkAppTest;

// Tests that hosted apps are not web apps.
IN_PROC_BROWSER_TEST_P(HostedAppTest, NotWebApp) {
  SetupApp("app");
  EXPECT_TRUE(app_->is_hosted_app());
  EXPECT_FALSE(app_->from_bookmark());
  EXPECT_FALSE(registrar().IsInstalled(app_->id()));
}

class HostedAppTestWithAutoupgradesDisabled : public HostedOrBookmarkAppTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HostedOrBookmarkAppTest::SetUpCommandLine(command_line);
    feature_list.InitAndDisableFeature(
        blink::features::kMixedContentAutoupgrade);
  }

 private:
  base::test::ScopedFeatureList feature_list;
};

IN_PROC_BROWSER_TEST_P(HostedAppTestWithAutoupgradesDisabled,
                       ShouldShowCustomTabBarMixedContent) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/");

  SetupAppWithURL(app_url);

  // Navigate to another page on the same origin, but with mixed content; the
  // toolbar should be shown.
  NavigateAndCheckForToolbar(
      app_browser_,
      https_server()->GetURL("app.com",
                             "/ssl/page_displays_insecure_content.html"),
      true);
}

IN_PROC_BROWSER_TEST_P(HostedAppTestWithAutoupgradesDisabled,
                       ShouldShowCustomTabBarDynamicMixedContent) {
  ASSERT_TRUE(https_server()->Start());
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");

  SetupAppWithURL(app_url);

  // Navigate to a page on the same origin. Since mixed content hasn't been
  // loaded yet, the toolbar shouldn't be shown.
  NavigateAndCheckForToolbar(app_browser_, app_url, false);

  // Load mixed content; now the toolbar should be shown.
  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(TryToLoadImage(
      web_contents, embedded_test_server()->GetURL("foo.com", kImagePath)));
  EXPECT_TRUE(app_browser_->app_controller()->ShouldShowCustomTabBar());
}

IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       ShouldShowCustomTabBarForHTTPAppSameOrigin) {
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL app_url =
      embedded_test_server()->GetURL("app.com", "/simple.html");
  SetupAppWithURL(app_url);

  // Navigate to the app's launch page; the toolbar should be visible, even
  // though it exactly matches the site, because it is not secure.
  NavigateAndCheckForToolbar(app_browser_, app_url, true);
}

// Flaky, mostly on Windows: http://crbug.com/1032319
#if defined(OS_WIN)
#define MAYBE_ShouldShowCustomTabBarForHTTPAppHTTPSUrl \
  DISABLED_ShouldShowCustomTabBarForHTTPAppHTTPSUrl
#else
#define MAYBE_ShouldShowCustomTabBarForHTTPAppHTTPSUrl \
  ShouldShowCustomTabBarForHTTPAppHTTPSUrl
#endif
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       MAYBE_ShouldShowCustomTabBarForHTTPAppHTTPSUrl) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");

  GURL::Replacements scheme_http;
  scheme_http.SetSchemeStr("http");

  // Create an app that has the same port and origin as `app_url` but with a
  // "http" scheme.
  SetupAppWithURL(app_url.ReplaceComponents(scheme_http));

  // Navigate to the https version of the site; the toolbar should
  // be hidden, as it is a more secure version of the site.
  NavigateAndCheckForToolbar(app_browser_, app_url, false);
}

IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       ShouldShowCustomTabBarForHTTPSAppSameOrigin) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");
  SetupAppWithURL(app_url);

  // Navigate to the app's launch page; the toolbar should be hidden.
  NavigateAndCheckForToolbar(app_browser_, app_url, false);
}

// Check that the toolbar is shown correctly for HTTPS apps when they
// navigate to a HTTP page on the same origin.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       ShouldShowCustomTabBarForHTTPSAppHTTPUrl) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");
  SetupAppWithURL(app_url);

  GURL::Replacements scheme_http;
  scheme_http.SetSchemeStr("http");

  // Navigate to the http version of the site; the toolbar should
  // be visible for the https version as it is not secure.
  NavigateAndCheckForToolbar(app_browser_,
                             app_url.ReplaceComponents(scheme_http), true);
}

// Check that the toolbar is shown correctly for apps that specify start
// URLs without the 'www.' prefix.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest,
                       ShouldShowCustomTabBarForAppWithoutWWW) {
  ASSERT_TRUE(https_server()->Start());

  const GURL app_url = https_server()->GetURL("app.com", "/simple.html");
  SetupAppWithURL(app_url);

  // Navigate to the app's launch page; the toolbar should be hidden.
  NavigateAndCheckForToolbar(app_browser_, app_url, false);

  // Navigate to the app's launch page with the 'www.' prefix; the toolbar
  // should be hidden.
  NavigateAndCheckForToolbar(
      app_browser_, https_server()->GetURL("www.app.com", "/simple.html"),
      false);

  // Navigate to different origin; the toolbar should now be visible.
  NavigateAndCheckForToolbar(
      app_browser_, https_server()->GetURL("www.foo.com", "/simple.html"),
      true);
}

// Check that a subframe on a regular web page can navigate to a URL that
// redirects to a platform app.  https://crbug.com/721949.
IN_PROC_BROWSER_TEST_P(HostedOrBookmarkAppTest, SubframeRedirectsToHostedApp) {
  // This test only applies to hosted apps.
  if (app_type() != AppType::HOSTED_APP)
    return;

  ASSERT_TRUE(embedded_test_server()->Start());

  // Set up an app which covers app.com URLs.
  GURL app_url = embedded_test_server()->GetURL("app.com", "/title1.html");
  SetupAppWithURL(app_url);

  // Navigate a regular tab to a page with a subframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/iframe.html");
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  NavigateToURLAndWait(browser(), url);

  // Navigate the subframe to a URL that redirects to a URL in the hosted app's
  // web extent.
  GURL redirect_url = embedded_test_server()->GetURL(
      "bar.com", "/server-redirect?" + app_url.spec());
  EXPECT_TRUE(NavigateIframeToURL(tab, "test", redirect_url));

  // Ensure that the frame navigated successfully and that it has correct
  // content.
  RenderFrameHost* subframe = content::ChildFrameAt(tab->GetMainFrame(), 0);
  EXPECT_EQ(app_url, subframe->GetLastCommittedURL());
  EXPECT_EQ(
      "This page has no title.",
      EvalJs(subframe, "document.body.innerText.trim();").ExtractString());
}

// Tests that platform apps can still load mixed content.
IN_PROC_BROWSER_TEST_P(HostedAppTestWithAutoupgradesDisabled,
                       MixedContentInPlatformApp) {
  ASSERT_TRUE(https_server()->Start());
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL app_url = GetMixedContentAppURL();

  ui_test_utils::UrlLoadObserver url_observer(
      app_url, content::NotificationService::AllSources());
  SetupAppWithURL(app_url);
  url_observer.Wait();

  web_app::CheckMixedContentLoaded(app_browser_);
}

// Common app manifest for HostedAppProcessModelTests.
constexpr const char kHostedAppProcessModelManifest[] =
    R"( { "name": "Hosted App Process Model Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["*://app.site.com/frame_tree",  "*://isolated.site.com/"]
            }
          } )";

// This set of tests verifies the hosted app process model behavior in various
// isolation modes.
//
// Relevant frames in the tests:
// - |app| - app.site.com/frame_tree/cross_origin_but_same_site_frames.html
//           Main frame, launch URL of the hosted app (i.e. app.launch.web_url).
// - |same_dir| - app.site.com/frame_tree/simple.htm
//                Another URL, but still covered by hosted app's web extent
//                (i.e. by app.urls).
// - |diff_dir| - app.site.com/save_page/a.htm
//                Same origin as |same_dir| and |app|, but not covered by app's
//                extent.
// - |same_site| - other.site.com/title1.htm
//                 Different origin, but same site as |app|, |same_dir|,
//                 |diff_dir|.
// - |isolated| - isolated.site.com/title1.htm
//                Within app's extent, but belongs to an isolated origin.
//                Some tests also use isolated.site.com/title1.htm (defined by
//                |isolated_url_outside_app_|), which is an isolated origin
//                outside the app's extent.
// - |cross_site| - cross.domain.com/title1.htm
//                  Cross-site from all the other frames.

class HostedAppProcessModelTest : public HostedOrBookmarkAppTest {
 public:
  HostedAppProcessModelTest() {}
  ~HostedAppProcessModelTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    HostedOrBookmarkAppTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    std::string origin1 =
        embedded_test_server()->GetURL("isolated.site.com", "/").spec();
    std::string origin2 =
        embedded_test_server()->GetURL("isolated.foo.com", "/").spec();
    std::string origin_list =
        base::StringPrintf("%s,%s", origin1.c_str(), origin2.c_str());
    command_line->AppendSwitchASCII(switches::kIsolateOrigins, origin_list);
  }

  void SetUpOnMainThread() override {
    HostedOrBookmarkAppTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");

    // Some tests make requests to URLs that purposefully end with a double
    // slash to test this edge case (note that "//" is a valid path).  Install
    // a custom handler to return dummy content for such requests before
    // starting the test server.
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url == "//") {
            return std::make_unique<net::test_server::RawHttpResponse>(
                "HTTP/1.1 200 OK", "Hello there!");
          }
          return nullptr;
        }));

    embedded_test_server()->StartAcceptingConnections();

    should_swap_for_cross_site_ = content::AreAllSitesIsolatedForTesting();

    process_map_ = extensions::ProcessMap::Get(browser()->profile());

    same_dir_url_ = embedded_test_server()->GetURL("app.site.com",
                                                   "/frame_tree/simple.htm");
    diff_dir_url_ =
        embedded_test_server()->GetURL("app.site.com", "/save_page/a.htm");
    same_site_url_ =
        embedded_test_server()->GetURL("other.site.com", "/title1.html");
    isolated_url_ =
        embedded_test_server()->GetURL("isolated.site.com", "/title1.html");
    isolated_url_outside_app_ =
        embedded_test_server()->GetURL("isolated.foo.com", "/title1.html");
    cross_site_url_ =
        embedded_test_server()->GetURL("cross.domain.com", "/title1.html");
  }

  // Opens a popup from |rfh| to |url|, verifies whether it should stay in the
  // same process as |rfh| and whether it should be in an app process, and then
  // closes the popup.
  void TestPopupProcess(RenderFrameHost* rfh,
                        const GURL& url,
                        bool expect_same_process,
                        bool expect_app_process) {
    content::WebContentsAddedObserver tab_added_observer;
    ASSERT_TRUE(
        content::ExecuteScript(rfh, "window.open('" + url.spec() + "');"));
    content::WebContents* new_tab = tab_added_observer.GetWebContents();
    ASSERT_TRUE(new_tab);
    EXPECT_TRUE(WaitForLoadStop(new_tab));
    EXPECT_EQ(url, new_tab->GetLastCommittedURL());
    RenderFrameHost* new_rfh = new_tab->GetMainFrame();

    EXPECT_EQ(expect_same_process, rfh->GetProcess() == new_rfh->GetProcess())
        << " for " << url << " from " << rfh->GetLastCommittedURL();

    EXPECT_EQ(expect_app_process,
              process_map_->Contains(new_rfh->GetProcess()->GetID()))
        << " for " << url << " from " << rfh->GetLastCommittedURL();
    EXPECT_EQ(expect_app_process,
              new_rfh->GetSiteInstance()->GetSiteURL().SchemeIs(
                  extensions::kExtensionScheme))
        << " for " << url << " from " << rfh->GetLastCommittedURL();

    content::WebContentsDestroyedWatcher watcher(new_tab);
    ASSERT_TRUE(content::ExecuteScript(new_rfh, "window.close();"));
    watcher.Wait();
  }

  // Creates a subframe underneath |parent_rfh| to |url|, verifies whether it
  // should stay in the same process as |parent_rfh| and whether it should be in
  // an app process, and returns the subframe RFH.
  RenderFrameHost* TestSubframeProcess(RenderFrameHost* parent_rfh,
                                       const GURL& url,
                                       bool expect_same_process,
                                       bool expect_app_process) {
    return TestSubframeProcess(parent_rfh, url, "", expect_same_process,
                               expect_app_process);
  }

  RenderFrameHost* TestSubframeProcess(RenderFrameHost* parent_rfh,
                                       const GURL& url,
                                       const std::string& element_id,
                                       bool expect_same_process,
                                       bool expect_app_process) {
    WebContents* web_contents = WebContents::FromRenderFrameHost(parent_rfh);
    content::TestNavigationObserver nav_observer(web_contents, 1);
    std::string script = "var f = document.createElement('iframe');";
    if (!element_id.empty())
      script += "f.id = '" + element_id + "';";
    script += "f.src = '" + url.spec() + "';";
    script += "document.body.appendChild(f);";
    EXPECT_TRUE(ExecuteScript(parent_rfh, script));
    nav_observer.Wait();

    RenderFrameHost* subframe = content::FrameMatchingPredicate(
        web_contents, base::Bind(&content::FrameHasSourceUrl, url));

    EXPECT_EQ(expect_same_process,
              parent_rfh->GetProcess() == subframe->GetProcess())
        << " for " << url << " from " << parent_rfh->GetLastCommittedURL();

    EXPECT_EQ(expect_app_process,
              process_map_->Contains(subframe->GetProcess()->GetID()))
        << " for " << url << " from " << parent_rfh->GetLastCommittedURL();
    EXPECT_EQ(expect_app_process,
              subframe->GetSiteInstance()->GetSiteURL().SchemeIs(
                  extensions::kExtensionScheme))
        << " for " << url << " from " << parent_rfh->GetLastCommittedURL();

    return subframe;
  }

 protected:
  bool should_swap_for_cross_site_;

  extensions::ProcessMap* process_map_;

  GURL same_dir_url_;
  GURL diff_dir_url_;
  GURL same_site_url_;
  GURL isolated_url_;
  GURL isolated_url_outside_app_;
  GURL cross_site_url_;

 private:
  DISALLOW_COPY_AND_ASSIGN(HostedAppProcessModelTest);
};

// Tests that same-site iframes stay inside the hosted app process, even when
// they are not within the hosted app's extent.  This allows same-site scripting
// to work and avoids unnecessary OOPIFs.  Also tests that isolated origins in
// iframes do not stay in the app's process, nor do cross-site iframes in modes
// that require them to swap.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest, IframesInsideHostedApp) {
  // Set up and launch the hosted app.
  GURL url = embedded_test_server()->GetURL(
      "app.site.com", "/frame_tree/cross_origin_but_same_site_frames.html");

  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(
      base::StringPrintf(kHostedAppProcessModelManifest, url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  auto find_frame = [web_contents](const std::string& name) {
    return content::FrameMatchingPredicate(
        web_contents, base::Bind(&content::FrameMatchesName, name));
  };
  RenderFrameHost* app = web_contents->GetMainFrame();
  RenderFrameHost* same_dir = find_frame("SameOrigin-SamePath");
  RenderFrameHost* diff_dir = find_frame("SameOrigin-DifferentPath");
  RenderFrameHost* same_site = find_frame("OtherSubdomain-SameSite");
  RenderFrameHost* isolated = find_frame("Isolated-SameSite");
  RenderFrameHost* cross_site = find_frame("CrossSite");

  // Sanity-check sites of all relevant frames to verify test setup.
  GURL app_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), app->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, app_site.scheme());

  GURL same_dir_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), same_dir->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, same_dir_site.scheme());
  EXPECT_EQ(same_dir_site, app_site);

  GURL diff_dir_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), diff_dir->GetLastCommittedURL());
  EXPECT_NE(extensions::kExtensionScheme, diff_dir_site.scheme());
  EXPECT_NE(diff_dir_site, app_site);

  GURL same_site_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), same_site->GetLastCommittedURL());
  EXPECT_NE(extensions::kExtensionScheme, same_site_site.scheme());
  EXPECT_NE(same_site_site, app_site);
  EXPECT_EQ(same_site_site, diff_dir_site);

  // The isolated.site.com iframe is covered by the hosted app's extent, so it
  // uses a chrome-extension site URL, but the site URL should be different
  // from the main app's site URL, as this iframe is expected to go into a
  // separate app process, because isolated.site.com matches an isolated
  // origin.
  GURL isolated_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), isolated->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, isolated_site.scheme());
  EXPECT_NE(isolated_site, app_site);
  EXPECT_NE(isolated_site, diff_dir_site);

  GURL cross_site_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), cross_site->GetLastCommittedURL());
  EXPECT_NE(cross_site_site, app_site);
  EXPECT_NE(cross_site_site, same_site_site);

  // Verify that |same_dir| and |diff_dir| have the same origin according to
  // |window.origin| (even though they have different |same_dir_site| and
  // |diff_dir_site|).
  std::string same_dir_origin;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      same_dir, "domAutomationController.send(window.origin)",
      &same_dir_origin));
  std::string diff_dir_origin;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      diff_dir, "domAutomationController.send(window.origin)",
      &diff_dir_origin));
  EXPECT_EQ(diff_dir_origin, same_dir_origin);

  // Verify that (1) all same-site iframes stay in the process, (2) isolated
  // origin iframe does not, and (3) cross-site iframe leaves if the process
  // model calls for it.
  EXPECT_EQ(same_dir->GetProcess(), app->GetProcess());
  EXPECT_EQ(diff_dir->GetProcess(), app->GetProcess());
  EXPECT_EQ(same_site->GetProcess(), app->GetProcess());
  EXPECT_NE(isolated->GetProcess(), app->GetProcess());
  if (should_swap_for_cross_site_)
    EXPECT_NE(cross_site->GetProcess(), app->GetProcess());
  else
    EXPECT_EQ(cross_site->GetProcess(), app->GetProcess());

  // The isolated origin iframe's process should be in the ProcessMap, since
  // the isolated origin is covered by the app's extent.
  EXPECT_TRUE(process_map_->Contains(isolated->GetProcess()->GetID()));

  // If we swapped processes for the |cross_site| iframe, its process should
  // not be on the ProcessMap.
  if (should_swap_for_cross_site_)
    EXPECT_FALSE(process_map_->Contains(cross_site->GetProcess()->GetID()));

  // Verify that |same_dir| and |diff_dir| can script each other.
  // (they should - they have the same origin).
  std::string inner_text_from_other_frame;
  const std::string r_script =
      R"( var w = window.open('', 'SameOrigin-SamePath');
          domAutomationController.send(w.document.body.innerText); )";
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      diff_dir, r_script, &inner_text_from_other_frame));
  EXPECT_EQ("Simple test page.", inner_text_from_other_frame);
}

// Check that if a hosted app has an iframe, and that iframe navigates to URLs
// that are same-site with the app, these navigations ends up in the app
// process.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest,
                       IframeNavigationsInsideHostedApp) {
  // Set up and launch the hosted app.
  GURL app_url =
      embedded_test_server()->GetURL("app.site.com", "/frame_tree/simple.htm");

  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(kHostedAppProcessModelManifest,
                                                app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  RenderFrameHost* app = web_contents->GetMainFrame();

  // Add a data: URL subframe.  This should stay in the app process.
  TestSubframeProcess(app, GURL("data:text/html,foo"), "test_iframe",
                      true /* expect_same_process */,
                      true /* expect_app_process */);

  // Navigate iframe to a non-app-but-same-site-with-app URL and check that it
  // stays in the parent process.
  {
    SCOPED_TRACE("... for data: -> diff_dir");
    EXPECT_TRUE(
        NavigateIframeToURL(web_contents, "test_iframe", diff_dir_url_));
    EXPECT_EQ(ChildFrameAt(app, 0)->GetProcess(), app->GetProcess());
  }

  // Navigate the iframe to an isolated origin to force an OOPIF.
  {
    SCOPED_TRACE("... for diff_dir -> isolated");
    EXPECT_TRUE(
        NavigateIframeToURL(web_contents, "test_iframe", isolated_url_));
    EXPECT_NE(ChildFrameAt(app, 0)->GetProcess(), app->GetProcess());
  }

  // Navigate the iframe to an app URL. This should go back to the app process.
  {
    SCOPED_TRACE("... for isolated -> same_dir");
    EXPECT_TRUE(
        NavigateIframeToURL(web_contents, "test_iframe", same_dir_url_));
    EXPECT_EQ(ChildFrameAt(app, 0)->GetProcess(), app->GetProcess());
  }

  // Navigate the iframe back to the OOPIF again.
  {
    SCOPED_TRACE("... for same_dir -> isolated");
    EXPECT_TRUE(
        NavigateIframeToURL(web_contents, "test_iframe", isolated_url_));
    EXPECT_NE(ChildFrameAt(app, 0)->GetProcess(), app->GetProcess());
  }

  // Navigate iframe to a non-app-but-same-site-with-app URL and check that it
  // also goes back to the parent process.
  {
    SCOPED_TRACE("... for isolated -> diff_dir");
    EXPECT_TRUE(
        NavigateIframeToURL(web_contents, "test_iframe", diff_dir_url_));
    EXPECT_EQ(ChildFrameAt(app, 0)->GetProcess(), app->GetProcess());
  }
}

// Tests that popups opened within a hosted app behave as expected.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest, PopupsInsideHostedApp) {
  // Set up and launch the hosted app.
  GURL url = embedded_test_server()->GetURL(
      "app.site.com", "/frame_tree/cross_origin_but_same_site_frames.html");

  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(
      base::StringPrintf(kHostedAppProcessModelManifest, url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  auto find_frame = [web_contents](const std::string& name) {
    return content::FrameMatchingPredicate(
        web_contents, base::Bind(&content::FrameMatchesName, name));
  };
  RenderFrameHost* app = web_contents->GetMainFrame();
  RenderFrameHost* same_dir = find_frame("SameOrigin-SamePath");
  RenderFrameHost* diff_dir = find_frame("SameOrigin-DifferentPath");
  RenderFrameHost* same_site = find_frame("OtherSubdomain-SameSite");
  RenderFrameHost* isolated = find_frame("Isolated-SameSite");
  RenderFrameHost* cross_site = find_frame("CrossSite");

  {
    SCOPED_TRACE("... for same_dir popup");
    TestPopupProcess(app, same_dir_url_, true, true);
  }
  {
    SCOPED_TRACE("... for diff_dir popup");
    TestPopupProcess(app, diff_dir_url_, true, true);
  }
  {
    SCOPED_TRACE("... for same_site popup");
    TestPopupProcess(app, same_site_url_, true, true);
  }
  // The isolated origin URL for isolated.site.com should swap processes, but
  // since it's covered by the app's extent, it should still be in a
  // (different) app process.
  {
    SCOPED_TRACE("... for isolated_url popup");
    TestPopupProcess(app, isolated_url_, false, true);
  }
  // The isolated origin URL for isolated.foo.com should swap processes, and
  // since it's not covered by the app's extent, it should not be in an app
  // process.
  {
    SCOPED_TRACE("... for isolated_url_outside_app popup");
    TestPopupProcess(app, isolated_url_outside_app_, false, false);
  }
  // For cross-site, the resulting popup should swap processes and not be in
  // the app process.
  {
    SCOPED_TRACE("... for cross_site popup");
    TestPopupProcess(app, cross_site_url_, false, false);
  }

  // If the iframes open popups that are same-origin with themselves, the popups
  // should be in the same process as the respective iframes.
  {
    SCOPED_TRACE("... for same_dir iframe popup");
    TestPopupProcess(same_dir, same_dir_url_, true, true);
  }
  {
    SCOPED_TRACE("... for diff_dir iframe popup");
    TestPopupProcess(diff_dir, diff_dir_url_, true, true);
  }
  {
    SCOPED_TRACE("... for same_site iframe popup");
    TestPopupProcess(same_site, same_site_url_, true, true);
  }
  {
    SCOPED_TRACE("... for isolated_url iframe popup");
    TestPopupProcess(isolated, isolated_url_, true, true);
  }
  {
    SCOPED_TRACE("... for cross_site iframe popup");
    TestPopupProcess(cross_site, cross_site_url_, true,
                     !should_swap_for_cross_site_);
  }
}

// Tests that hosted app URLs loaded in iframes of non-app pages won't cause an
// OOPIF unless there is another reason to create it, but popups from outside
// the app will swap into the app.
// TODO(crbug.com/807471): Flaky on Windows 7.
#if defined(OS_WIN)
#define MAYBE_FromOutsideHostedApp DISABLED_FromOutsideHostedApp
#else
#define MAYBE_FromOutsideHostedApp FromOutsideHostedApp
#endif
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest, MAYBE_FromOutsideHostedApp) {
  // Set up and launch the hosted app.
  GURL app_url =
      embedded_test_server()->GetURL("app.site.com", "/frame_tree/simple.htm");

  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(kHostedAppProcessModelManifest,
                                                app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  // Starting same-origin but outside the app, popups should swap to the app.
  {
    SCOPED_TRACE("... from diff_dir");
    ui_test_utils::NavigateToURL(app_browser_, diff_dir_url_);
    RenderFrameHost* main_frame = web_contents->GetMainFrame();
    EXPECT_FALSE(main_frame->GetSiteInstance()->GetSiteURL().SchemeIs(
        extensions::kExtensionScheme));
    TestPopupProcess(main_frame, app_url, false, true);
    // Subframes in the app should not swap.
    RenderFrameHost* diff_dir_rfh =
        TestSubframeProcess(main_frame, app_url, true, false);
    // Popups from the subframe, though same-origin, should swap to the app.
    // See https://crbug.com/89272.
    TestPopupProcess(diff_dir_rfh, app_url, false, true);
  }

  // Starting same-site but outside the app, popups should swap to the app.
  {
    SCOPED_TRACE("... from same_site");
    ui_test_utils::NavigateToURL(app_browser_, same_site_url_);
    RenderFrameHost* main_frame = web_contents->GetMainFrame();
    EXPECT_FALSE(main_frame->GetSiteInstance()->GetSiteURL().SchemeIs(
        extensions::kExtensionScheme));
    TestPopupProcess(main_frame, app_url, false, true);
    // Subframes in the app should not swap.
    RenderFrameHost* same_site_rfh =
        TestSubframeProcess(main_frame, app_url, true, false);
    // Popups from the subframe should swap to the app, as above.
    TestPopupProcess(same_site_rfh, app_url, false, true);
  }

  // Starting on an isolated origin outside the app's extent, popups should
  // swap to the app.
  {
    SCOPED_TRACE("... from isolated_url");
    ui_test_utils::NavigateToURL(app_browser_, isolated_url_outside_app_);
    RenderFrameHost* main_frame = web_contents->GetMainFrame();
    EXPECT_FALSE(main_frame->GetSiteInstance()->GetSiteURL().SchemeIs(
        extensions::kExtensionScheme));
    TestPopupProcess(main_frame, app_url, false, true);
    // Subframes in the app should swap process.
    // TODO(creis): Perhaps this OOPIF should not be an app process?
    RenderFrameHost* isolated_rfh =
        TestSubframeProcess(main_frame, app_url, false, true);
    // Popups from the subframe into the app should be in the app process.
    TestPopupProcess(isolated_rfh, app_url, true, true);
  }

  // Starting cross-site, popups should swap to the app.
  {
    SCOPED_TRACE("... from cross_site");
    ui_test_utils::NavigateToURL(app_browser_, cross_site_url_);
    RenderFrameHost* main_frame = web_contents->GetMainFrame();
    EXPECT_FALSE(main_frame->GetSiteInstance()->GetSiteURL().SchemeIs(
        extensions::kExtensionScheme));
    TestPopupProcess(main_frame, app_url, false, true);
    // Subframes in the app should swap if the process model needs it.
    // TODO(creis): Perhaps this OOPIF should not be an app process?
    RenderFrameHost* cross_site_rfh =
        TestSubframeProcess(main_frame, app_url, !should_swap_for_cross_site_,
                            should_swap_for_cross_site_);
    // Popups from the subframe into the app should be in the app process.
    TestPopupProcess(cross_site_rfh, app_url, should_swap_for_cross_site_,
                     true);
  }
}

// Tests that a packaged app is not considered an installed bookmark app.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest,
                       AppRegistrarExcludesPackaged) {
  SetupApp("https_app");
  EXPECT_FALSE(registrar().IsInstalled(app_->id()));
}

// Check that we can successfully complete a navigation to an app URL with a
// "//" path (on which GURL::Resolve() currently fails due to
// https://crbug.com/1034197), and that the resulting SiteInstance has a valid
// site URL. See https://crbug.com/1016954.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest,
                       NavigateToAppURLWithDoubleSlashPath) {
  // Set up and launch the hosted app.
  GURL app_url =
      embedded_test_server()->GetURL("app.site.com", "/frame_tree/simple.htm");
  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(kHostedAppProcessModelManifest,
                                                app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  // Navigate to a URL under the app's extent, but with a path (//) that
  // GURL::Resolve() fails to resolve against a relative URL (see the
  // explanation in https://crbug.com/1034197).  Avoid giving the "//" directly
  // to EmbeddedTestServer::GetURL(), which also uses GURL::Resolve()
  // internally and would otherwise produce an empty/invalid URL to navigate
  // to.
  GURL double_slash_path_app_url =
      embedded_test_server()->GetURL("isolated.site.com", "/");
  GURL::Replacements replace_path;
  replace_path.SetPathStr("//");
  double_slash_path_app_url =
      double_slash_path_app_url.ReplaceComponents(replace_path);

  ui_test_utils::NavigateToURL(browser(), double_slash_path_app_url);
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  RenderFrameHost* main_frame = contents->GetMainFrame();
  EXPECT_EQ(double_slash_path_app_url, main_frame->GetLastCommittedURL());

  // The resulting page should load in an app process, and the corresponding
  // SiteInstance's site URL should be a valid, non-empty chrome-extension://
  // URL with a valid host that corresponds to the app's ID.
  EXPECT_TRUE(process_map_->Contains(main_frame->GetProcess()->GetID()));
  EXPECT_FALSE(main_frame->GetSiteInstance()->GetSiteURL().is_empty());
  EXPECT_TRUE(main_frame->GetSiteInstance()->GetSiteURL().SchemeIs(
      extensions::kExtensionScheme));
  EXPECT_EQ(main_frame->GetSiteInstance()->GetSiteURL().host(), app_->id());
}

// Helper class that sets up two isolated origins, where one is a subdomain of
// the other: https://isolated.com and https://very.isolated.com.
class HostedAppIsolatedOriginTest : public HostedAppProcessModelTest {
 public:
  HostedAppIsolatedOriginTest() {}
  ~HostedAppIsolatedOriginTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    HostedOrBookmarkAppTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    GURL isolated_url = embedded_test_server()->GetURL("isolated.com", "/");
    GURL very_isolated_url =
        embedded_test_server()->GetURL("very.isolated.com", "/");
    std::string origin_list = base::StringPrintf(
        "%s,%s", isolated_url.spec().c_str(), very_isolated_url.spec().c_str());
    command_line->AppendSwitchASCII(switches::kIsolateOrigins, origin_list);
  }
};

// Check that a hosted app that is contained within an isolated.com isolated
// origin is allowed to load in a privileged app process. Also check that a
// very.isolated.com URL, which corresponds to very.isolated.com isolated
// origin but is outside the hosted app's extent, ends up in its own non-app
// process. See https://crbug.com/799638.
IN_PROC_BROWSER_TEST_P(HostedAppIsolatedOriginTest,
                       NestedIsolatedOriginStaysOutsideApp) {
  // Set up and launch the hosted app.
  GURL app_url =
      embedded_test_server()->GetURL("isolated.com", "/frame_tree/simple.htm");

  constexpr const char kHostedAppWithinIsolatedOriginManifest[] =
      R"( { "name": "Hosted App Within Isolated Origin Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://*.isolated.com/frame_tree"]
            }
          } )";
  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(
      kHostedAppWithinIsolatedOriginManifest, app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  // Check that the app loaded properly. Even though its URL is from an
  // isolated origin (isolated.com), it should go into an app process.
  RenderFrameHost* app = web_contents->GetMainFrame();
  EXPECT_EQ(extensions::kExtensionScheme,
            app->GetSiteInstance()->GetSiteURL().scheme());
  GURL app_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), app->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, app_site.scheme());
  EXPECT_TRUE(process_map_->Contains(app->GetProcess()->GetID()));

  // Add a same-site subframe on isolated.com outside the app's extent.  This
  // should stay in app process.
  GURL foo_isolated_url =
      embedded_test_server()->GetURL("foo.isolated.com", "/title1.html");
  TestSubframeProcess(app, foo_isolated_url, true /* expect_same_process */,
                      true /* expect_app_process */);

  // Add a subframe on very.isolated.com outside the app's extent.  Despite
  // being same-site, this matches a different, more specific isolated origin
  // and should go into a separate, non-app process.
  GURL very_isolated_url =
      embedded_test_server()->GetURL("very.isolated.com", "/title2.html");
  TestSubframeProcess(app, very_isolated_url, false /* expect_same_process */,
                      false /* expect_app_process */);

  // Add a subframe on very.isolated.com inside the app's extent.  Despite
  // being same-site, this matches a different, more specific isolated origin
  // and should go into a separate app process.
  GURL very_isolated_app_url = embedded_test_server()->GetURL(
      "very.isolated.com", "/frame_tree/simple.htm");
  TestSubframeProcess(app, very_isolated_app_url,
                      false /* expect_same_process */,
                      true /* expect_app_process */);

  // Similarly, a popup for very.isolated.com should go into a separate,
  // non-app process.
  TestPopupProcess(app, very_isolated_url, false /* expect_same_process */,
                   false /* expect_app_process */);

  // Navigating main frame from the app to very.isolated.com should also swap
  // processes to a non-app process.
  ui_test_utils::NavigateToURL(app_browser_, very_isolated_url);
  EXPECT_FALSE(process_map_->Contains(
      web_contents->GetMainFrame()->GetProcess()->GetID()));

  // Navigating main frame back to the app URL should go into an app process.
  ui_test_utils::NavigateToURL(app_browser_, app_url);
  EXPECT_TRUE(process_map_->Contains(
      web_contents->GetMainFrame()->GetProcess()->GetID()));
}

// Check that when a hosted app's extent contains multiple origins, one of
// which is an isolated origin, loading an app URL in that isolated origin
// won't later allow another origin in the app's extent to share the same app
// process.
IN_PROC_BROWSER_TEST_P(HostedAppIsolatedOriginTest,
                       AppBroaderThanIsolatedOrigin) {
  // Set up and launch the hosted app, with the launch URL being in an isolated
  // origin.
  GURL app_url =
      embedded_test_server()->GetURL("isolated.com", "/frame_tree/simple.htm");

  constexpr const char kHostedAppBroaderThanIsolatedOriginManifest[] =
      R"( { "name": "Hosted App Within Isolated Origin Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://*.isolated.com/frame_tree", "*://unisolated.com/"]
            }
          } )";
  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(
      kHostedAppBroaderThanIsolatedOriginManifest, app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath());

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  // The app URL should have loaded in an app process.
  RenderFrameHost* app = web_contents->GetMainFrame();
  EXPECT_TRUE(process_map_->Contains(app->GetProcess()->GetID()));
  EXPECT_EQ(extensions::kExtensionScheme,
            app->GetSiteInstance()->GetSiteURL().scheme());
  int first_app_process_id = app->GetProcess()->GetID();

  // Creating a subframe on unisolated.com should not be allowed to share the
  // main frame's app process, since we don't want the isolated.com isolated
  // origin to share a process with another origin.
  GURL unisolated_app_url =
      embedded_test_server()->GetURL("unisolated.com", "/title1.html");
  TestSubframeProcess(app, unisolated_app_url, false /* expect_same_process */,
                      true /* expect_app_process */);

  // Opening a popup or navigating to an app URL on unisolated.com should go
  // into a separate app process, different from the one that was used for
  // isolated.com.
  TestPopupProcess(app, unisolated_app_url, false /* expect_same_process */,
                   true /* expect_app_process */);

  ui_test_utils::NavigateToURL(app_browser_, unisolated_app_url);
  EXPECT_TRUE(process_map_->Contains(
      web_contents->GetMainFrame()->GetProcess()->GetID()));
  EXPECT_NE(first_app_process_id,
            web_contents->GetMainFrame()->GetProcess()->GetID());
}

class HostedAppSitePerProcessTest : public HostedAppProcessModelTest {
 public:
  HostedAppSitePerProcessTest() {}
  ~HostedAppSitePerProcessTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    HostedOrBookmarkAppTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    content::IsolateAllSitesForTesting(command_line);
  }
};

// Check that two different cross-site hosted apps won't share a process even
// when over process limit, when in --site-per-process mode.  See
// https://crbug.com/811939.
IN_PROC_BROWSER_TEST_P(HostedAppSitePerProcessTest,
                       DoNotShareProcessWhenOverProcessLimit) {
  // Set the process limit to 1.
  content::RenderProcessHost::SetMaxRendererProcessCount(1);

  // Set up and launch a hosted app covering foo.com.
  GURL foo_app_url(embedded_test_server()->GetURL("foo.com", "/title1.html"));
  constexpr const char kHostedAppManifest[] =
      R"( { "name": "Hosted App With SitePerProcess Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://%s/"]
            }
          } )";
  {
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(base::StringPrintf(
        kHostedAppManifest, foo_app_url.spec().c_str(), "foo.com"));
    SetupApp(test_app_dir.UnpackedPath());
  }
  content::WebContents* foo_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(foo_contents));

  // Set up and launch a hosted app covering bar.com.
  GURL bar_app_url(embedded_test_server()->GetURL("bar.com", "/title1.html"));
  {
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(base::StringPrintf(
        kHostedAppManifest, bar_app_url.spec().c_str(), "bar.com"));
    SetupApp(test_app_dir.UnpackedPath());
  }
  content::WebContents* bar_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(bar_contents));

  EXPECT_NE(foo_contents, bar_contents);
  EXPECT_NE(foo_contents->GetMainFrame()->GetSiteInstance(),
            bar_contents->GetMainFrame()->GetSiteInstance());
  EXPECT_EQ(foo_app_url, foo_contents->GetLastCommittedURL());
  EXPECT_EQ(bar_app_url, bar_contents->GetLastCommittedURL());

  // Under --site-per-process the two apps should load in separate processes,
  // even when over process limit.
  EXPECT_NE(foo_contents->GetMainFrame()->GetProcess(),
            bar_contents->GetMainFrame()->GetProcess());
}

// Check that when a hosted app covers multiple sites in its web extent, these
// sites do not share a process in site-per-process mode. See
// https://crbug.com/791796.
IN_PROC_BROWSER_TEST_P(HostedAppSitePerProcessTest,
                       DoNotShareProcessForDifferentSitesCoveredBySameApp) {
  // Set up a hosted app covering http://foo.com and http://bar.com, and launch
  // the app with a foo.com URL in a new window.
  GURL foo_app_url(embedded_test_server()->GetURL("foo.com", "/title1.html"));
  constexpr const char kHostedAppManifest[] =
      R"( { "name": "Hosted App With SitePerProcess Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://%s/", "http://%s/"]
            }
          } )";
  {
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(base::StringPrintf(
        kHostedAppManifest, foo_app_url.spec().c_str(), "foo.com", "bar.com"));
    SetupApp(test_app_dir.UnpackedPath());
  }
  content::WebContents* foo_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(foo_contents));
  EXPECT_EQ(foo_app_url, foo_contents->GetLastCommittedURL());

  // Now navigate original window to a bar.com app URL.
  GURL bar_app_url(embedded_test_server()->GetURL("bar.com", "/title2.html"));
  ui_test_utils::NavigateToURL(browser(), bar_app_url);
  content::WebContents* bar_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(bar_app_url, bar_contents->GetLastCommittedURL());
  EXPECT_NE(foo_contents, bar_contents);

  // Ensure the two pages don't share a process despite being from the same
  // app, since they are from different sites.
  EXPECT_NE(foo_contents->GetMainFrame()->GetSiteInstance(),
            bar_contents->GetMainFrame()->GetSiteInstance());
  auto* foo_process =
      foo_contents->GetMainFrame()->GetSiteInstance()->GetProcess();
  auto* bar_process =
      bar_contents->GetMainFrame()->GetSiteInstance()->GetProcess();
  EXPECT_NE(foo_process, bar_process);

  // Ensure each process only has access to its site's data.
  auto* policy = content::ChildProcessSecurityPolicy::GetInstance();
  EXPECT_TRUE(
      policy->CanAccessDataForOrigin(foo_process->GetID(), foo_app_url));
  EXPECT_FALSE(
      policy->CanAccessDataForOrigin(foo_process->GetID(), bar_app_url));
  EXPECT_FALSE(
      policy->CanAccessDataForOrigin(bar_process->GetID(), foo_app_url));
  EXPECT_TRUE(
      policy->CanAccessDataForOrigin(bar_process->GetID(), bar_app_url));

  // Both processes should still be app processes.
  auto* process_map = extensions::ProcessMap::Get(browser()->profile());
  EXPECT_TRUE(process_map->Contains(foo_process->GetID()));
  EXPECT_TRUE(process_map->Contains(bar_process->GetID()));
}

// Check that when a hosted app covers multiple sites in its web extent,
// navigating from one of these sites to another swaps processes.
IN_PROC_BROWSER_TEST_P(HostedAppSitePerProcessTest,
                       CrossSiteNavigationsWithinApp) {
  // Set up a hosted app covering http://foo.com/frame_tree and http://bar.com.
  GURL foo_app_url(
      embedded_test_server()->GetURL("foo.com", "/frame_tree/simple.htm"));
  GURL bar_app_url(embedded_test_server()->GetURL("bar.com", "/title2.html"));
  constexpr const char kHostedAppManifest[] =
      R"( { "name": "Hosted App With SitePerProcess Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://foo.com/frame_tree", "http://bar.com/"]
            }
          } )";
  {
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(
        base::StringPrintf(kHostedAppManifest, foo_app_url.spec().c_str()));
    SetupApp(test_app_dir.UnpackedPath());
  }

  // Navigate main window to a foo.com app URL.
  ui_test_utils::NavigateToURL(browser(), foo_app_url);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(foo_app_url, web_contents->GetLastCommittedURL());
  scoped_refptr<content::SiteInstance> foo_site_instance =
      web_contents->GetMainFrame()->GetSiteInstance();
  auto* foo_process = foo_site_instance->GetProcess();
  auto* process_map = extensions::ProcessMap::Get(browser()->profile());
  EXPECT_TRUE(process_map->Contains(foo_process->GetID()));

  // At this point the main frame process should have access to foo.com data
  // but not bar.com data.
  auto* policy = content::ChildProcessSecurityPolicy::GetInstance();
  EXPECT_TRUE(
      policy->CanAccessDataForOrigin(foo_process->GetID(), foo_app_url));
  EXPECT_FALSE(
      policy->CanAccessDataForOrigin(foo_process->GetID(), bar_app_url));

  // Ensure the current process is allowed to access cookies.
  EXPECT_TRUE(ExecuteScript(web_contents, "document.cookie = 'foo=bar';"));
  EXPECT_EQ("foo=bar", EvalJs(web_contents, "document.cookie"));

  // Now navigate to a bar.com app URL in the same BrowsingInstance.  Ensure
  // that this uses a new SiteInstance and process.
  {
    content::TestNavigationObserver observer(web_contents);
    EXPECT_TRUE(
        ExecuteScript(web_contents, "location = '" + bar_app_url.spec() + "'"));
    observer.Wait();
  }
  EXPECT_EQ(bar_app_url, web_contents->GetLastCommittedURL());
  scoped_refptr<content::SiteInstance> bar_site_instance =
      web_contents->GetMainFrame()->GetSiteInstance();
  EXPECT_NE(foo_site_instance, bar_site_instance);
  auto* bar_process = bar_site_instance->GetProcess();
  EXPECT_TRUE(process_map->Contains(bar_process->GetID()));
  EXPECT_NE(foo_process, bar_process);

  // At this point the main frame process should have access to bar.com data.
  EXPECT_TRUE(
      policy->CanAccessDataForOrigin(bar_process->GetID(), bar_app_url));
  EXPECT_FALSE(
      policy->CanAccessDataForOrigin(bar_process->GetID(), foo_app_url));

  // Ensure the current process is allowed to access cookies.
  EXPECT_TRUE(ExecuteScript(web_contents, "document.cookie = 'foo=bar';"));
  EXPECT_EQ("foo=bar", EvalJs(web_contents, "document.cookie"));

  // Now navigate from a foo.com app URL to a foo.com non-app URL.  Ensure that
  // there's a process swap from an app to a non-app process.
  ui_test_utils::NavigateToURL(browser(), foo_app_url);
  EXPECT_EQ(foo_app_url, web_contents->GetLastCommittedURL());
  foo_site_instance = web_contents->GetMainFrame()->GetSiteInstance();
  foo_process = foo_site_instance->GetProcess();
  EXPECT_TRUE(process_map->Contains(foo_process->GetID()));

  GURL foo_nonapp_url(
      embedded_test_server()->GetURL("foo.com", "/title1.html"));
  {
    content::TestNavigationObserver observer(web_contents);
    EXPECT_TRUE(ExecuteScript(web_contents,
                              "location = '" + foo_nonapp_url.spec() + "'"));
    observer.Wait();
  }
  EXPECT_EQ(foo_nonapp_url, web_contents->GetLastCommittedURL());
  EXPECT_NE(foo_site_instance, web_contents->GetMainFrame()->GetSiteInstance());
  auto* foo_nonapp_process = web_contents->GetMainFrame()->GetProcess();
  EXPECT_NE(foo_process, foo_nonapp_process);
  EXPECT_FALSE(process_map->Contains(foo_nonapp_process->GetID()));

  // Ensure the current non-app foo.com process is allowed to access foo.com
  // data.
  EXPECT_TRUE(policy->CanAccessDataForOrigin(foo_nonapp_process->GetID(),
                                             foo_nonapp_url));
  EXPECT_TRUE(ExecuteScript(web_contents, "document.cookie = 'foo=bar';"));
  EXPECT_EQ("foo=bar", EvalJs(web_contents, "document.cookie"));
}

// Check background page scriptability for a hosted app that covers multiple
// sites in its web extent.  When a hosted app page opens a background page,
// only same-site parts of the app should be able to script that background
// page. This behavior should be the same with and without --site-per-process.
IN_PROC_BROWSER_TEST_P(HostedAppProcessModelTest,
                       BackgroundPageWithAppCoveringDifferentSites) {
  // Set up a hosted app covering http://foo.com and http://bar.com.
  GURL foo_app_url(embedded_test_server()->GetURL("foo.com", "/title1.html"));
  constexpr const char kHostedAppManifest[] =
      R"( { "name": "Hosted App With SitePerProcess Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["http://foo.com/", "http://bar.com/"]
            },
            "permissions": ["background"]
          } )";
  {
    extensions::TestExtensionDir test_app_dir;
    test_app_dir.WriteManifest(
        base::StringPrintf(kHostedAppManifest, foo_app_url.spec().c_str()));
    SetupApp(test_app_dir.UnpackedPath());
  }

  // Set up three unrelated hosted app tabs in the main browser window:
  // foo.com, bar.com, and another one at foo.com.
  ui_test_utils::NavigateToURL(browser(), foo_app_url);
  content::WebContents* foo_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(foo_app_url, foo_contents->GetLastCommittedURL());

  GURL bar_app_url(embedded_test_server()->GetURL("bar.com", "/title2.html"));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), bar_app_url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* bar_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(bar_app_url, bar_contents->GetLastCommittedURL());
  EXPECT_NE(foo_contents, bar_contents);

  GURL foo_app_url2(embedded_test_server()->GetURL("foo.com", "/title3.html"));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), foo_app_url2, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* foo_contents2 =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(foo_app_url2, foo_contents2->GetLastCommittedURL());
  EXPECT_NE(foo_contents, foo_contents2);
  EXPECT_NE(bar_contents, foo_contents2);
  ASSERT_EQ(3, browser()->tab_strip_model()->count());

  // The two foo.com tabs should be in the same process even though they are
  // unrelated, since hosted apps use the process-per-site process model.
  auto* foo_process = foo_contents->GetMainFrame()->GetProcess();
  EXPECT_EQ(foo_process, foo_contents2->GetMainFrame()->GetProcess());
  EXPECT_FALSE(
      foo_contents->GetMainFrame()->GetSiteInstance()->IsRelatedSiteInstance(
          foo_contents2->GetMainFrame()->GetSiteInstance()));

  // The bar.com tab should be in a different process from the foo.com tabs.
  auto* bar_process = bar_contents->GetMainFrame()->GetProcess();
  EXPECT_NE(foo_process, bar_process);

  // Ensure all tabs are in app processes.
  auto* process_map = extensions::ProcessMap::Get(browser()->profile());
  EXPECT_TRUE(process_map->Contains(foo_process->GetID()));
  EXPECT_TRUE(process_map->Contains(bar_process->GetID()));

  // Open a background page from the first foo.com window.
  {
    content::TestNavigationObserver background_page_observer(nullptr);
    background_page_observer.StartWatchingNewWebContents();
    EXPECT_TRUE(content::ExecuteScript(
        foo_contents,
        "window.bg = window.open('/empty.html', 'bg', 'background');"));
    background_page_observer.Wait();
    EXPECT_EQ(embedded_test_server()->GetURL("foo.com", "/empty.html"),
              background_page_observer.last_navigation_url());

    // The background page shouldn't show up in the tab strip.
    ASSERT_EQ(3, browser()->tab_strip_model()->count());
  }

  // Script the background page from the first foo.com window and set a dummy
  // value.
  EXPECT_TRUE(content::ExecuteScript(foo_contents,
                                     "bg.document.body.innerText = 'foo'"));

  // Ensure that the second foo.com page can script the same background page
  // and retrieve the value.
  EXPECT_EQ("foo",
            content::EvalJs(foo_contents2,
                            "window.open('', 'bg').document.body.innerText"));

  // Ensure that the bar.com page cannot script this background page, since it
  // is cross-origin from it. The window lookup via window.open('', bg') should
  // be disallowed, resulting in a new popup instead, and the innerText value
  // from that should be empty.
  EXPECT_EQ("",
            content::EvalJs(bar_contents,
                            "window.open('', 'bg').document.body.innerText"));

  // Open another bar.com app URL in an unrelated tab.  This should share a
  // process with the first bar.com tab, due to hosted apps using
  // process-per-site.
  GURL bar_app_url2(embedded_test_server()->GetURL("bar.com", "/title3.html"));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), bar_app_url2, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* bar_contents2 =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(bar_app_url2, bar_contents2->GetLastCommittedURL());
  EXPECT_EQ(bar_process, bar_contents2->GetMainFrame()->GetProcess());
  EXPECT_FALSE(
      bar_contents->GetMainFrame()->GetSiteInstance()->IsRelatedSiteInstance(
          bar_contents2->GetMainFrame()->GetSiteInstance()));

  // Ensure bar.com tabs can open and script their open background page.
  {
    content::TestNavigationObserver background_page_observer(nullptr);
    background_page_observer.StartWatchingNewWebContents();
    EXPECT_TRUE(content::ExecuteScript(
        bar_contents,
        "window.bg = window.open('/empty.html', 'bg2', 'background');"));
    background_page_observer.Wait();
    EXPECT_EQ(embedded_test_server()->GetURL("bar.com", "/empty.html"),
              background_page_observer.last_navigation_url());
  }
  EXPECT_TRUE(content::ExecuteScript(bar_contents,
                                     "bg.document.body.innerText = 'bar'"));
  EXPECT_EQ("bar",
            content::EvalJs(bar_contents2,
                            "window.open('', 'bg2').document.body.innerText"));
}

INSTANTIATE_TEST_SUITE_P(All,
                         HostedOrBookmarkAppTest,
                         ::testing::Values(AppType::HOSTED_APP,
                                           AppType::BOOKMARK_APP));

INSTANTIATE_TEST_SUITE_P(All,
                         HostedAppTest,
                         ::testing::Values(AppType::HOSTED_APP));

INSTANTIATE_TEST_SUITE_P(All,
                         HostedAppTestWithAutoupgradesDisabled,
                         ::testing::Values(AppType::HOSTED_APP));

INSTANTIATE_TEST_SUITE_P(
    All,
    HostedAppProcessModelTest,
    ::testing::Values(AppType::HOSTED_APP));

INSTANTIATE_TEST_SUITE_P(
    All,
    HostedAppIsolatedOriginTest,
    ::testing::Values(AppType::HOSTED_APP));

INSTANTIATE_TEST_SUITE_P(
    All,
    HostedAppSitePerProcessTest,
    ::testing::Values(AppType::HOSTED_APP));
