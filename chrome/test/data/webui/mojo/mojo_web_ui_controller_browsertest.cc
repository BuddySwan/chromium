// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/webui/mojo_web_ui_controller.h"

#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chrome/browser/bad_message.h"
#include "chrome/browser/chrome_browser_interface_binders.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/data/grit/webui_test_resources.h"
#include "chrome/test/data/webui/mojo/foobar.mojom.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "content/public/test/test_utils.h"
#include "services/service_manager/public/cpp/binder_map.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace {

// WebUIController that provides the Foo Mojo API.
class FooUI : public ui::MojoWebUIController, public ::test::mojom::Foo {
 public:
  explicit FooUI(content::WebUI* web_ui)
      : ui::MojoWebUIController(web_ui), foo_receiver_(this) {
    content::WebUIDataSource* data_source =
        content::WebUIDataSource::Create("foo");
    data_source->SetDefaultResource(IDR_MOJO_WEB_UI_CONTROLLER_TEST_HTML);
    data_source->DisableContentSecurityPolicy();
    data_source->AddResourcePath("foobar.mojom-lite.js",
                                 IDR_FOOBAR_MOJO_LITE_JS);
    content::WebUIDataSource::Add(web_ui->GetWebContents()->GetBrowserContext(),
                                  data_source);
  }

  void BindInterface(mojo::PendingReceiver<::test::mojom::Foo> receiver) {
    foo_receiver_.Bind(std::move(receiver));
  }

  // ::test::mojom::Foo:
  void GetFoo(GetFooCallback callback) override {
    std::move(callback).Run("foofoo");
  }

  WEB_UI_CONTROLLER_TYPE_DECL();

 private:
  mojo::Receiver<::test::mojom::Foo> foo_receiver_;

  DISALLOW_COPY_AND_ASSIGN(FooUI);
};

WEB_UI_CONTROLLER_TYPE_IMPL(FooUI)

// WebUIController that provides the Foo and Bar Mojo APIs.
class FooBarUI : public ui::MojoWebUIController,
                 public ::test::mojom::Foo,
                 public ::test::mojom::Bar {
 public:
  explicit FooBarUI(content::WebUI* web_ui)
      : ui::MojoWebUIController(web_ui),
        foo_receiver_(this),
        bar_receiver_(this) {
    content::WebUIDataSource* data_source =
        content::WebUIDataSource::Create("foobar");
    data_source->SetDefaultResource(IDR_MOJO_WEB_UI_CONTROLLER_TEST_HTML);
    data_source->DisableContentSecurityPolicy();
    data_source->AddResourcePath("foobar.mojom-lite.js",
                                 IDR_FOOBAR_MOJO_LITE_JS);
    content::WebUIDataSource::Add(web_ui->GetWebContents()->GetBrowserContext(),
                                  data_source);
  }

  void BindInterface(mojo::PendingReceiver<::test::mojom::Foo> receiver) {
    foo_receiver_.Bind(std::move(receiver));
  }

  void BindInterface(mojo::PendingReceiver<::test::mojom::Bar> receiver) {
    bar_receiver_.Bind(std::move(receiver));
  }

  // ::test::mojom::Foo:
  void GetFoo(GetFooCallback callback) override {
    std::move(callback).Run("foobarfoo");
  }

  // ::test::mojom::Bar:
  void GetBar(GetBarCallback callback) override {
    std::move(callback).Run("foobarbar");
  }

  WEB_UI_CONTROLLER_TYPE_DECL();

 private:
  mojo::Receiver<::test::mojom::Foo> foo_receiver_;
  mojo::Receiver<::test::mojom::Bar> bar_receiver_;

  DISALLOW_COPY_AND_ASSIGN(FooBarUI);
};

WEB_UI_CONTROLLER_TYPE_IMPL(FooBarUI)

// WebUIControllerFactory that serves our TestWebUIController.
class TestWebUIControllerFactory : public content::WebUIControllerFactory {
 public:
  TestWebUIControllerFactory() = default;

  std::unique_ptr<content::WebUIController> CreateWebUIControllerForURL(
      content::WebUI* web_ui,
      const GURL& url) override {
    if (url.host_piece() == "foo")
      return std::make_unique<FooUI>(web_ui);
    if (url.host_piece() == "foobar")
      return std::make_unique<FooBarUI>(web_ui);

    return nullptr;
  }

  content::WebUI::TypeID GetWebUIType(content::BrowserContext* browser_context,
                                      const GURL& url) override {
    if (url.SchemeIs(content::kChromeUIScheme))
      return reinterpret_cast<content::WebUI::TypeID>(1);

    return content::WebUI::kNoWebUI;
  }

  bool UseWebUIForURL(content::BrowserContext* browser_context,
                      const GURL& url) override {
    return url.SchemeIs(content::kChromeUIScheme);
  }
  bool UseWebUIBindingsForURL(content::BrowserContext* browser_context,
                              const GURL& url) override {
    return url.SchemeIs(content::kChromeUIScheme);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestWebUIControllerFactory);
};

}  // namespace

class MojoWebUIControllerBrowserTest : public InProcessBrowserTest {
 public:
  MojoWebUIControllerBrowserTest() {
    factory_ = std::make_unique<TestWebUIControllerFactory>();
    content::WebUIControllerFactory::RegisterFactory(factory_.get());
  }

  void SetUpOnMainThread() override {
    base::FilePath pak_path;
    ASSERT_TRUE(base::PathService::Get(base::DIR_MODULE, &pak_path));
    pak_path = pak_path.AppendASCII("browser_tests.pak");
    ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        pak_path, ui::SCALE_FACTOR_NONE);

    content::SetBrowserClientForTesting(&test_content_browser_client_);
  }

 private:
  class TestContentBrowserClient : public ChromeContentBrowserClient {
   public:
    TestContentBrowserClient() = default;
    TestContentBrowserClient(const TestContentBrowserClient&) = delete;
    TestContentBrowserClient& operator=(const TestContentBrowserClient&) =
        delete;
    ~TestContentBrowserClient() override = default;

    void RegisterBrowserInterfaceBindersForFrame(
        content::RenderFrameHost* render_frame_host,
        service_manager::BinderMapWithContext<content::RenderFrameHost*>* map)
        override {
      ChromeContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
          render_frame_host, map);
      chrome::internal::RegisterWebUIControllerInterfaceBinder<
          ::test::mojom::Bar, FooBarUI>(map);
      chrome::internal::RegisterWebUIControllerInterfaceBinder<
          ::test::mojom::Foo, FooUI, FooBarUI>(map);
    }
  };

  std::unique_ptr<TestWebUIControllerFactory> factory_;

  TestContentBrowserClient test_content_browser_client_;
};

// Attempting to access bindings succeeds for 2 allowed interfaces.
IN_PROC_BROWSER_TEST_F(MojoWebUIControllerBrowserTest, BindingsAccess) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(NavigateToURL(web_contents, content::GetWebUIURL("foobar")));

  EXPECT_EQ("foobarfoo",
            content::EvalJs(web_contents,
                            "(async () => {"
                            "  let fooRemote = test.mojom.Foo.getRemote();"
                            "  let resp = await fooRemote.getFoo();"
                            "  return resp.value;"
                            "})()"));

  EXPECT_EQ("foobarbar",
            content::EvalJs(web_contents,
                            "(async () => {"
                            "  let barRemote = test.mojom.Bar.getRemote();"
                            "  let resp = await barRemote.getBar();"
                            "  return resp.value;"
                            "})()"));
}

// Attempting to access bindings crashes the renderer when access not allowed.
IN_PROC_BROWSER_TEST_F(MojoWebUIControllerBrowserTest,
                       BindingsAccessViolation) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(NavigateToURL(web_contents, content::GetWebUIURL("foo")));

  EXPECT_EQ("foofoo",
            content::EvalJs(web_contents,
                            "(async () => {"
                            "  let fooRemote = test.mojom.Foo.getRemote();"
                            "  let resp = await fooRemote.getFoo();"
                            "  return resp.value;"
                            "})()"));

  content::ScopedAllowRendererCrashes allow;

  // Attempt to get a remote for a disallowed interface.
  EXPECT_FALSE(content::EvalJs(web_contents,
                               "(async () => {"
                               "  let barRemote = test.mojom.Bar.getRemote();"
                               "  let resp = await barRemote.getBar();"
                               "  return resp.value;"
                               "})()")
                   .error.empty());
  EXPECT_TRUE(web_contents->IsCrashed());
}
