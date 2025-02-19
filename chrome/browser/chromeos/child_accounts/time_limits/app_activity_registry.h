// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMITS_APP_ACTIVITY_REGISTRY_H_
#define CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMITS_APP_ACTIVITY_REGISTRY_H_

#include <map>
#include <memory>
#include <set>

#include "base/observer_list_types.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/child_accounts/time_limits/app_activity_report_interface.h"
#include "chrome/browser/chromeos/child_accounts/time_limits/app_service_wrapper.h"
#include "chrome/browser/chromeos/child_accounts/time_limits/app_types.h"

namespace aura {
class Window;
}  // namespace aura

namespace enterprise_management {
class ChildStatusReportRequest;
}  // namespace enterprise_management

namespace base {
class OneShotTimer;
}  // namespace base

class PrefRegistrySimple;
class Profile;

namespace chromeos {
namespace app_time {

class AppTimeLimitsWhitelistPolicyWrapper;
class AppTimeNotificationDelegate;
class PersistedAppInfo;

// Keeps track of app activity and time limits information.
// Stores app activity between user session. Information about uninstalled apps
// are removed from the registry after activity was uploaded to server or after
// 30 days if upload did not happen.
class AppActivityRegistry : public AppServiceWrapper::EventListener {
 public:
  // Used for tests to get internal implementation details.
  class TestApi {
   public:
    explicit TestApi(AppActivityRegistry* registry);
    ~TestApi();

    const base::Optional<AppLimit>& GetAppLimit(const AppId& app_id) const;
    base::Optional<base::TimeDelta> GetTimeLeft(const AppId& app_id) const;
    void SaveAppActivity();

   private:
    AppActivityRegistry* const registry_;
  };

  // Interface for the observers interested in the changes of apps state.
  class AppStateObserver : public base::CheckedObserver {
   public:
    AppStateObserver() = default;
    AppStateObserver(const AppStateObserver&) = delete;
    AppStateObserver& operator=(const AppStateObserver&) = delete;
    ~AppStateObserver() override = default;

    // Called when state of the app with |app_id| changed to |kLimitReached|.
    // |was_active| indicates whether the app was active before reaching the
    // limit.
    virtual void OnAppLimitReached(const AppId& app_id,
                                   base::TimeDelta time_limit,
                                   bool was_active) = 0;

    // Called when state of the app with |app_id| is no longer |kLimitReached|.
    virtual void OnAppLimitRemoved(const AppId& app_id) = 0;

    // Called when new app was installed.
    virtual void OnAppInstalled(const AppId& app_id) = 0;
  };

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  AppActivityRegistry(AppServiceWrapper* app_service_wrapper,
                      AppTimeNotificationDelegate* notification_delegate,
                      Profile* profile);
  AppActivityRegistry(const AppActivityRegistry&) = delete;
  AppActivityRegistry& operator=(const AppActivityRegistry&) = delete;
  ~AppActivityRegistry() override;

  // AppServiceWrapper::EventListener:
  void OnAppInstalled(const AppId& app_id) override;
  void OnAppUninstalled(const AppId& app_id) override;
  void OnAppAvailable(const AppId& app_id) override;
  void OnAppBlocked(const AppId& app_id) override;
  void OnAppActive(const AppId& app_id,
                   aura::Window* window,
                   base::Time timestamp) override;
  void OnAppInactive(const AppId& app_id,
                     aura::Window* window,
                     base::Time timestamp) override;

  bool IsAppInstalled(const AppId& app_id) const;
  bool IsAppAvailable(const AppId& app_id) const;
  bool IsAppBlocked(const AppId& app_id) const;
  bool IsAppTimeLimitReached(const AppId& app_id) const;
  bool IsAppActive(const AppId& app_id) const;
  bool IsWhitelistedApp(const AppId& app_id) const;

  // Manages AppStateObservers.
  void AddAppStateObserver(AppStateObserver* observer);
  void RemoveAppStateObserver(AppStateObserver* observer);

  // Returns the total active time for the application since the last time limit
  // reset.
  base::TimeDelta GetActiveTime(const AppId& app_id) const;

  AppState GetAppState(const AppId& app_id) const;

  // Returns the vector of paused applications.
  std::vector<PauseAppInfo> GetPausedApps(bool show_pause_dialog) const;

  // Populates |report| with collected app activity. Returns whether any data
  // were reported.
  AppActivityReportInterface::ReportParams GenerateAppActivityReport(
      enterprise_management::ChildStatusReportRequest* report) const;

  // Removes data older than |timestamp| from the registry.
  // Removes entries for uninstalled apps if there is no more relevant activity
  // data left.
  void CleanRegistry(base::Time timestamp);

  // Updates time limits for all installed apps.
  // Apps not present in |app_limits| are treated as they do not have limit set.
  void UpdateAppLimits(const std::map<AppId, AppLimit>& app_limits);

  // Sets time limit for app identified with |app_id|.
  // Does not affect limits of any other app. Not specified |app_limit| means
  // that app does not have limit set. Does not affect limits of any other app.
  void SetAppLimit(const AppId& app_id,
                   const base::Optional<AppLimit>& app_limit);

  // Reset time has been reached at |timestamp|.
  void OnResetTimeReached(base::Time timestamp);

  // Called from WebTimeActivityProvider to update chrome app state.
  void OnChromeAppActivityChanged(ChromeAppActivityState state,
                                  base::Time timestamp);

  // Whitelisted applications changed. Called by AppTimeController.
  void OnTimeLimitWhitelistChanged(
      const AppTimeLimitsWhitelistPolicyWrapper& wrapper);

 private:
  // Bundles detailed data stored for a specific app.
  struct AppDetails {
    AppDetails();
    explicit AppDetails(const AppActivity& activity);
    AppDetails(const AppDetails&) = delete;
    AppDetails& operator=(const AppDetails&) = delete;
    ~AppDetails();

    // Resets the time limit check timer.
    void ResetTimeCheck();

    // Checks |limit| and |activity| to determine if the limit was reached.
    bool IsLimitReached() const;

    // Checks if |limit| is equal to |another_limit| with exception for the
    // timestamp (that does not indicate that limit changed).
    bool IsLimitEqual(const base::Optional<AppLimit>& another_limit) const;

    // Contains information about current app state and logged activity.
    AppActivity activity{AppState::kAvailable};

    // Contains the set of active windows for the application.
    std::set<aura::Window*> active_windows;

    // Contains information about restriction set for the app.
    base::Optional<AppLimit> limit;

    // Timer set up for when the app time limit is expected to be reached and
    // preceding notifications.
    std::unique_ptr<base::OneShotTimer> app_limit_timer;
  };

  // Adds an ap to the registry if it does not exist.
  void Add(const AppId& app_id);

  // Convenience methods to access state of the app identified by |app_id|.
  // Should only be called if app exists in the registry.
  void SetAppState(const AppId& app_id, AppState app_state);

  // Methods to set the application as active and inactive respectively.
  void SetAppActive(const AppId& app_id, base::Time timestamp);
  void SetAppInactive(const AppId& app_id, base::Time timestamp);

  base::Optional<base::TimeDelta> GetTimeLeftForApp(const AppId& app_id) const;

  // Schedules a time limit check for application when it becomes active.
  void ScheduleTimeLimitCheckForApp(const AppId& app_id);

  // Checks the limit and shows notification if needed.
  void CheckTimeLimitForApp(const AppId& app_id);

  // Shows notification about time limit updates for the app if there were
  // relevant changes between |old_limit| and |new_limit|.
  void ShowLimitUpdatedNotificationIfNeeded(
      const AppId& app_id,
      const base::Optional<AppLimit>& old_limit,
      const base::Optional<AppLimit>& new_limit);

  base::TimeDelta GetWebActiveRunningTime() const;

  void WebTimeLimitReached(base::Time timestamp);

  // Initializes |activity_registry_| from the stored values in user pref.
  // Installed applications, their AppStates and their running active times will
  // be restored.
  void InitializeRegistryFromPref();

  // Updates |AppActivity::active_times_| to include the current activity up to
  // |timestamp| then creates the most up to date instance of PersistedAppInfo.
  PersistedAppInfo GetPersistedAppInfoForApp(const AppId& app_id,
                                             base::Time timestamp);

  // Saves app activity into user preference.
  void SaveAppActivity();

  Profile* const profile_;

  // Owned by AppTimeController.
  AppServiceWrapper* const app_service_wrapper_;

  // Notification delegate.
  AppTimeNotificationDelegate* const notification_delegate_;

  // Observers to be notified about app state changes.
  base::ObserverList<AppStateObserver> app_state_observers_;

  std::map<AppId, AppDetails> activity_registry_;

  // Newly installed applications which have not yet been added to the user
  // pref.
  std::vector<AppId> newly_installed_apps_;
};

}  // namespace app_time
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMITS_APP_ACTIVITY_REGISTRY_H_
