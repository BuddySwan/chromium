// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/assistant/assistant_test_mixin.h"

#include <utility>

#include "ash/assistant/model/ui/assistant_card_element.h"
#include "ash/assistant/ui/assistant_ui_constants.h"
#include "ash/assistant/ui/main_stage/assistant_ui_element_view.h"
#include "ash/public/cpp/ash_pref_names.h"
#include "ash/public/cpp/assistant/assistant_state.h"
#include "ash/public/cpp/test/assistant_test_api.h"
#include "ash/public/mojom/assistant_state_controller.mojom-shared.h"
#include "base/auto_reset.h"
#include "base/run_loop.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/login/test/embedded_test_server_mixin.h"
#include "chrome/browser/chromeos/login/test/fake_gaia_mixin.h"
#include "chrome/browser/chromeos/login/test/login_manager_mixin.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/mixin_based_in_process_browser_test.h"
#include "chromeos/constants/chromeos_switches.h"
#include "chromeos/login/auth/user_context.h"
#include "components/account_id/account_id.h"
#include "components/language/core/browser/pref_names.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/events/test/event_generator.h"

namespace chromeos {
namespace assistant {

namespace {

constexpr const char kTestUser[] = "test_user@gmail.com";
constexpr const char kTestUserGaiaId[] = "test_user@gaia.id";

LoginManagerMixin::TestUserInfo GetTestUserInfo() {
  return LoginManagerMixin::TestUserInfo(
      AccountId::FromUserEmailGaiaId(kTestUser, kTestUserGaiaId));
}

bool Equals(const char* left, const char* right) {
  return strcmp(left, right) == 0;
}

// Waiter that blocks in the |Wait| method until a given |mojom::AssistantState|
// is reached, or until a timeout is hit.
// On timeout this will abort the test with a useful error message.
class AssistantStatusWaiter : private ash::AssistantStateObserver {
 public:
  AssistantStatusWaiter(ash::AssistantState* state,
                        ash::mojom::AssistantState expected_status)
      : state_(state), expected_status_(expected_status) {
    state_->AddObserver(this);
  }

  ~AssistantStatusWaiter() override { state_->RemoveObserver(this); }

  void RunUntilExpectedStatus() {
    if (state_->assistant_state() == expected_status_)
      return;

    // Wait until we're ready or we hit the timeout.
    base::RunLoop run_loop;
    base::AutoReset<base::OnceClosure> quit_loop(&quit_loop_,
                                                 run_loop.QuitClosure());
    EXPECT_NO_FATAL_FAILURE(run_loop.Run())
        << "Failed waiting for AssistantStatus |" << expected_status_ << "|. "
        << "Current status is |" << state_->assistant_state() << "|. "
        << "One possible cause is that you're using an expired access token.";
  }

 private:
  void OnAssistantStatusChanged(ash::mojom::AssistantState status) override {
    if (status == expected_status_ && quit_loop_)
      std::move(quit_loop_).Run();
  }

  ash::AssistantState* const state_;
  ash::mojom::AssistantState const expected_status_;

  base::OnceClosure quit_loop_;
};

// Base class that observes all new responses being displayed under the
// |parent_view|, and searches for any of the given |expected_responses|, or
// until a timeout is hit. On timeout this will abort the test with a useful
// error message.
// The derived classes must implement the logic to extract the response from a
// given view.
class ResponseWaiter : private views::ViewObserver {
 public:
  ResponseWaiter(views::View* parent_view,
                 const std::vector<std::string>& expected_responses)
      : parent_view_(parent_view), expected_responses_(expected_responses) {
    parent_view_->AddObserver(this);
  }
  ~ResponseWaiter() override {
    if (parent_view_)
      parent_view_->RemoveObserver(this);
  }

  void RunUntilResponseReceived() {
    if (HasExpectedResponse())
      return;

    // Wait until we're ready or we hit the timeout.
    base::RunLoop run_loop;
    base::AutoReset<base::OnceClosure> quit_loop(&quit_loop_,
                                                 run_loop.QuitClosure());
    EXPECT_NO_FATAL_FAILURE(run_loop.Run())
        << "Failed waiting for Assistant response.\n"
        << "Expected any of " << FormatExpectedResponses() << ".\n"
        << "Got \"" << GetResponseText() << "\"";
  }

 private:
  // views::ViewObserver overrides:
  void OnViewHierarchyChanged(
      views::View* observed_view,
      const views::ViewHierarchyChangedDetails& details) override {
    if (quit_loop_ && HasExpectedResponse())
      std::move(quit_loop_).Run();
  }

  void OnViewIsDeleting(views::View* observed_view) override {
    DCHECK(observed_view == parent_view_);

    if (quit_loop_) {
      FAIL() << parent_view_->GetClassName() << " is deleted "
             << "before receiving the Assistant response.\n"
             << "Expected any of " << FormatExpectedResponses() << ".\n"
             << "Got \"" << GetResponseText() << "\"";
      std::move(quit_loop_).Run();
    }

    parent_view_ = nullptr;
  }

  bool HasExpectedResponse() const {
    std::string response = GetResponseText();

    for (const std::string& expected : expected_responses_) {
      if (response.find(expected) != std::string::npos)
        return true;
    }
    return false;
  }

  std::string GetResponseText() const {
    return GetResponseTextRecursive(parent_view_);
  }

  std::string GetResponseTextRecursive(views::View* view) const {
    base::Optional<std::string> response_maybe = GetResponseTextOfView(view);
    if (response_maybe) {
      return response_maybe.value() + "\n";
    } else {
      std::string result;
      for (views::View* child : view->children())
        result += GetResponseTextRecursive(child);
      return result;
    }
  }

  virtual base::Optional<std::string> GetResponseTextOfView(
      views::View* view) const = 0;

  std::string FormatExpectedResponses() const {
    std::string result = "{\n";
    for (const std::string& expected : expected_responses_)
      result += "    \"" + expected + "\",\n";
    result += "}";
    return result;
  }

  views::View* parent_view_;
  std::vector<std::string> expected_responses_;

  base::OnceClosure quit_loop_;
};

class TextResponseWaiter : public ResponseWaiter {
 public:
  using ResponseWaiter::ResponseWaiter;

 private:
  base::Optional<std::string> GetResponseTextOfView(
      views::View* view) const override {
    if (Equals(view->GetClassName(), "AssistantTextElementView")) {
      auto* text_view = static_cast<ash::AssistantUiElementView*>(view);
      return text_view->ToStringForTesting();
    }

    return base::nullopt;
  }
};

class CardResponseWaiter : public ResponseWaiter {
 public:
  using ResponseWaiter::ResponseWaiter;

  base::Optional<std::string> GetResponseTextOfView(
      views::View* view) const override {
    if (Equals(view->GetClassName(), "AssistantCardElementView")) {
      auto* card_view = static_cast<ash::AssistantUiElementView*>(view);
      return card_view->ToStringForTesting();
    }

    return base::nullopt;
  }
};

template <typename T>
void CheckResult(base::OnceClosure quit,
                 T expected_value,
                 base::RepeatingCallback<T()> value_callback) {
  if (expected_value == value_callback.Run()) {
    std::move(quit).Run();
    return;
  }

  // Check again in the future
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(CheckResult<T>, std::move(quit), expected_value,
                     value_callback),
      base::TimeDelta::FromMilliseconds(10));
}

}  // namespace

// Test mixin for the browser tests that logs in the given user and issues
// refresh and access tokens for this user.
class LoggedInUserMixin : public InProcessBrowserTestMixin {
 public:
  LoggedInUserMixin(InProcessBrowserTestMixinHost* host,
                    InProcessBrowserTest* test_base,
                    LoginManagerMixin::TestUserInfo user,
                    net::EmbeddedTestServer* embedded_test_server)
      : InProcessBrowserTestMixin(host),
        login_manager_(host, {user}),
        test_server_(host, embedded_test_server),
        fake_gaia_(host, embedded_test_server),
        user_(user),
        test_base_(test_base),
        user_context_(LoginManagerMixin::CreateDefaultUserContext(user)) {
    // Tell LoginManagerMixin to launch the browser when the user is logged in.
    login_manager_.set_should_launch_browser(true);
  }

  ~LoggedInUserMixin() override = default;

  void SetAccessToken(std::string token) { access_token_ = token; }

  void SetUpOnMainThread() override {
    // By default, browser tests block anything that doesn't go to localhost, so
    // account.google.com requests would never reach fake GAIA server without
    // this.
    test_base_->host_resolver()->AddRule("*", "127.0.0.1");

    LogIn();
    SetupFakeGaia();

    // Ensure test_base_->browser() returns the browser of the logged in user
    // session.
    test_base_->SelectFirstBrowser();
  }

  void LogIn() {
    user_context_.SetRefreshToken(kRefreshToken);
    bool success = login_manager_.LoginAndWaitForActiveSession(user_context_);
    EXPECT_TRUE(success) << "Failed to log in as test user.";
  }

  void SetupFakeGaia() {
    FakeGaia::AccessTokenInfo token_info;
    token_info.token = access_token_;
    token_info.audience = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
    token_info.email = user_context_.GetAccountId().GetUserEmail();
    token_info.any_scope = true;
    token_info.expires_in = kAccessTokenExpiration;

    fake_gaia_.fake_gaia()->MapEmailToGaiaId(user_.account_id.GetUserEmail(),
                                             user_.account_id.GetGaiaId());
    fake_gaia_.fake_gaia()->IssueOAuthToken(kRefreshToken, token_info);
  }

 private:
  const char* kRefreshToken = FakeGaiaMixin::kFakeRefreshToken;
  const int kAccessTokenExpiration = FakeGaiaMixin::kFakeAccessTokenExpiration;

  LoginManagerMixin login_manager_;
  EmbeddedTestServerSetupMixin test_server_;
  FakeGaiaMixin fake_gaia_;

  LoginManagerMixin::TestUserInfo user_;
  InProcessBrowserTest* const test_base_;
  UserContext user_context_;
  std::string access_token_{FakeGaiaMixin::kFakeAllScopeAccessToken};
};

AssistantTestMixin::AssistantTestMixin(
    InProcessBrowserTestMixinHost* host,
    InProcessBrowserTest* test_base,
    net::EmbeddedTestServer* embedded_test_server,
    FakeS3Mode mode)
    : InProcessBrowserTestMixin(host),
      fake_s3_server_(),
      mode_(mode),
      test_api_(ash::AssistantTestApi::Create()),
      user_mixin_(std::make_unique<LoggedInUserMixin>(host,
                                                      test_base,
                                                      GetTestUserInfo(),
                                                      embedded_test_server)) {}

AssistantTestMixin::~AssistantTestMixin() = default;

void AssistantTestMixin::SetUpCommandLine(base::CommandLine* command_line) {
  // Prevent the Assistant setup flow dialog from popping up immediately on user
  // start - otherwise the Assistant can not be started.
  command_line->AppendSwitch(switches::kOobeSkipPostLogin);
}

void AssistantTestMixin::SetUpOnMainThread() {
  fake_s3_server_.Setup(mode_);
  user_mixin_->SetAccessToken(fake_s3_server_.GetAccessToken());
  test_api_->DisableAnimations();
}

void AssistantTestMixin::TearDownOnMainThread() {
  DisableAssistant();
  fake_s3_server_.Teardown();
}

void AssistantTestMixin::StartAssistantAndWaitForReady(
    base::TimeDelta wait_timeout) {
  const base::test::ScopedRunLoopTimeout run_timeout(FROM_HERE, wait_timeout);

  // Note: You might be tempted to call this function from SetUpOnMainThread(),
  // but that will not work as the Assistant service can not start until
  // |BrowserTestBase| calls InitializeNetworkProcess(), which it only does
  // after SetUpOnMainThread() finishes.

  test_api_->SetAssistantEnabled(true);
  SetPreferVoice(false);

  AssistantStatusWaiter waiter(test_api_->GetAssistantState(),
                               ash::mojom::AssistantState::NEW_READY);
  waiter.RunUntilExpectedStatus();

  // With the warmer welcome enabled the Assistant service will start an
  // interaction that will never complete (as our tests finish too soon).
  // This in turn causes the FakeS3Server to not remember this interaction when
  // running in |kRecord| mode, which then causes interaction failures in
  // |kReplay| mode, potentially leading to a deadlock (see b/144872676).
  DisableWarmerWelcome();
}

void AssistantTestMixin::SetPreferVoice(bool prefer_voice) {
  test_api_->SetPreferVoice(prefer_voice);
}

void AssistantTestMixin::SendTextQuery(const std::string& query) {
  test_api_->SendTextQuery(query);
}

template <typename T>
void AssistantTestMixin::ExpectResult(
    T expected_value,
    base::RepeatingCallback<T()> value_callback) {
  const base::test::ScopedRunLoopTimeout run_timeout(FROM_HERE,
                                                     kDefaultWaitTimeout);

  // Wait until we're ready or we hit the timeout.
  base::RunLoop run_loop;
  CheckResult(run_loop.QuitClosure(), expected_value, value_callback);

  EXPECT_NO_FATAL_FAILURE(run_loop.Run())
      << "Failed waiting for expected result.\n"
      << "Expected \"" << expected_value << "\"\n"
      << "Got \"" << value_callback.Run() << "\"";
}

template void AssistantTestMixin::ExpectResult<bool>(
    bool expected_value,
    base::RepeatingCallback<bool()> value_callback);

template <typename T>
T AssistantTestMixin::SyncCall(
    base::OnceCallback<void(base::OnceCallback<void(T)>)> func) {
  const base::test::ScopedRunLoopTimeout run_timeout(FROM_HERE,
                                                     kDefaultWaitTimeout);

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  T result;
  auto callback = base::BindOnce(
      [](T* result_ptr, base::OnceClosure quit_closure, T result_value) {
        *result_ptr = result_value;
        std::move(quit_closure).Run();
      },
      &result, run_loop.QuitClosure());

  std::move(func).Run(std::move(callback));

  EXPECT_NO_FATAL_FAILURE(run_loop.Run())
      << "Failed waiting for async callback to return.\n";

  return result;
}

template base::Optional<double> AssistantTestMixin::SyncCall(
    base::OnceCallback<void(base::OnceCallback<void(base::Optional<double>)>)>
        func);

void AssistantTestMixin::ExpectCardResponse(
    const std::string& expected_response,
    base::TimeDelta wait_timeout) {
  const base::test::ScopedRunLoopTimeout run_timeout(FROM_HERE, wait_timeout);
  CardResponseWaiter waiter(test_api_->ui_element_container(),
                            {expected_response});
  waiter.RunUntilResponseReceived();
}

void AssistantTestMixin::ExpectTextResponse(
    const std::string& expected_response,
    base::TimeDelta wait_timeout) {
  ExpectAnyOfTheseTextResponses({expected_response}, wait_timeout);
}

void AssistantTestMixin::ExpectAnyOfTheseTextResponses(
    const std::vector<std::string>& expected_responses,
    base::TimeDelta wait_timeout) {
  const base::test::ScopedRunLoopTimeout run_timeout(FROM_HERE, wait_timeout);
  TextResponseWaiter waiter(test_api_->ui_element_container(),
                            expected_responses);
  waiter.RunUntilResponseReceived();
}

void AssistantTestMixin::PressAssistantKey() {
  SendKeyPress(ui::VKEY_ASSISTANT);
}

bool AssistantTestMixin::IsVisible() {
  return test_api_->IsVisible();
}

PrefService* AssistantTestMixin::GetUserPreferences() {
  return ProfileManager::GetPrimaryUserProfile()->GetPrefs();
}

void AssistantTestMixin::SendKeyPress(ui::KeyboardCode key) {
  ui::test::EventGenerator event_generator(test_api_->root_window());
  event_generator.PressKey(key, /*flags=*/ui::EF_NONE);
}

void AssistantTestMixin::DisableAssistant() {
  // First disable Assistant in the settings.
  test_api_->SetAssistantEnabled(false);

  // Then wait for the Service to shutdown.
  AssistantStatusWaiter waiter(test_api_->GetAssistantState(),
                               ash::mojom::AssistantState::NOT_READY);
  waiter.RunUntilExpectedStatus();
}

void AssistantTestMixin::DisableWarmerWelcome() {
  // To disable the warmer welcome, we spoof that it has already been
  // triggered too many times.
  GetUserPreferences()->SetInteger(
      ash::prefs::kAssistantNumWarmerWelcomeTriggered,
      ash::assistant::ui::kWarmerWelcomesMaxTimesTriggered);
}

}  // namespace assistant
}  // namespace chromeos
