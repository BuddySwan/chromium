// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/activity_log_private/activity_log_private_api.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/event_router_forwarder.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/common/extensions/api/activity_log_private.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/hashed_extension_id.h"

namespace extensions {

namespace activity_log_private = api::activity_log_private;

using activity_log_private::ActivityResultSet;
using activity_log_private::ExtensionActivity;
using activity_log_private::Filter;

static base::LazyInstance<BrowserContextKeyedAPIFactory<ActivityLogAPI>>::
    DestructorAtExit g_activity_log_private_api_factory =
        LAZY_INSTANCE_INITIALIZER;

// static
BrowserContextKeyedAPIFactory<ActivityLogAPI>*
ActivityLogAPI::GetFactoryInstance() {
  return g_activity_log_private_api_factory.Pointer();
}

template <>
void
BrowserContextKeyedAPIFactory<ActivityLogAPI>::DeclareFactoryDependencies() {
  DependsOn(ExtensionsBrowserClient::Get()->GetExtensionSystemFactory());
  DependsOn(ActivityLog::GetFactoryInstance());
}

ActivityLogAPI::ActivityLogAPI(content::BrowserContext* context)
    : browser_context_(context), initialized_(false) {
  if (!EventRouter::Get(browser_context_)) {  // Check for testing.
    DVLOG(1) << "ExtensionSystem event_router does not exist.";
    return;
  }
  activity_log_ = extensions::ActivityLog::GetInstance(browser_context_);
  DCHECK(activity_log_);
  EventRouter::Get(browser_context_)->RegisterObserver(
      this, activity_log_private::OnExtensionActivity::kEventName);
  activity_log_->AddObserver(this);
  initialized_ = true;
}

ActivityLogAPI::~ActivityLogAPI() {
}

void ActivityLogAPI::Shutdown() {
  if (!initialized_) {  // Check for testing.
    DVLOG(1) << "ExtensionSystem event_router does not exist.";
    return;
  }
  EventRouter::Get(browser_context_)->UnregisterObserver(this);
  activity_log_->RemoveObserver(this);
}

// static
bool ActivityLogAPI::IsExtensionWhitelisted(const std::string& extension_id) {
  // TODO(devlin): Pass in a HashedExtensionId to avoid this conversion.
  return FeatureProvider::GetPermissionFeatures()
      ->GetFeature("activityLogPrivate")
      ->IsIdInAllowlist(HashedExtensionId(extension_id));
}

void ActivityLogAPI::OnListenerAdded(const EventListenerInfo& details) {
  if (activity_log_->has_listeners())
    return;
  StartOrStopListeningForExtensionActivities();
}

void ActivityLogAPI::OnListenerRemoved(const EventListenerInfo& details) {
  StartOrStopListeningForExtensionActivities();
}

void ActivityLogAPI::StartOrStopListeningForExtensionActivities() {
  EventRouter* event_router = EventRouter::Get(browser_context_);
  activity_log_->SetHasListeners(event_router->HasEventListener(
      activity_log_private::OnExtensionActivity::kEventName));
}

void ActivityLogAPI::OnExtensionActivity(scoped_refptr<Action> activity) {
  std::unique_ptr<base::ListValue> value(new base::ListValue());
  ExtensionActivity activity_arg = activity->ConvertToExtensionActivity();
  value->Append(activity_arg.ToValue());
  auto event = std::make_unique<Event>(
      events::ACTIVITY_LOG_PRIVATE_ON_EXTENSION_ACTIVITY,
      activity_log_private::OnExtensionActivity::kEventName, std::move(value),
      browser_context_);
  EventRouter::Get(browser_context_)->BroadcastEvent(std::move(event));
}

ExtensionFunction::ResponseAction
ActivityLogPrivateGetExtensionActivitiesFunction::Run() {
  std::unique_ptr<activity_log_private::GetExtensionActivities::Params> params(
      activity_log_private::GetExtensionActivities::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  // Get the arguments in the right format.
  Filter filter = std::move(params->filter);
  Action::ActionType action_type = Action::ACTION_API_CALL;
  switch (filter.activity_type) {
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_API_CALL:
      action_type = Action::ACTION_API_CALL;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_API_EVENT:
      action_type = Action::ACTION_API_EVENT;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_CONTENT_SCRIPT:
      action_type = Action::ACTION_CONTENT_SCRIPT;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_DOM_ACCESS:
      action_type = Action::ACTION_DOM_ACCESS;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_DOM_EVENT:
      action_type = Action::ACTION_DOM_EVENT;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_WEB_REQUEST:
      action_type = Action::ACTION_WEB_REQUEST;
      break;
    case activity_log_private::EXTENSION_ACTIVITY_FILTER_ANY:
    default:
      action_type = Action::ACTION_ANY;
  }
  std::string extension_id =
      filter.extension_id ? *filter.extension_id : std::string();
  std::string api_call = filter.api_call ? *filter.api_call : std::string();
  std::string page_url = filter.page_url ? *filter.page_url : std::string();
  std::string arg_url = filter.arg_url ? *filter.arg_url : std::string();
  int days_ago = -1;
  if (filter.days_ago)
    days_ago = *filter.days_ago;

  // Call the ActivityLog.
  ActivityLog* activity_log = ActivityLog::GetInstance(browser_context());
  DCHECK(activity_log);
  activity_log->GetFilteredActions(
      extension_id,
      action_type,
      api_call,
      page_url,
      arg_url,
      days_ago,
      base::Bind(
          &ActivityLogPrivateGetExtensionActivitiesFunction::OnLookupCompleted,
          this));

  return RespondLater();
}

void ActivityLogPrivateGetExtensionActivitiesFunction::OnLookupCompleted(
    std::unique_ptr<std::vector<scoped_refptr<Action>>> activities) {
  // Convert Actions to ExtensionActivities.
  std::vector<ExtensionActivity> result_arr;
  for (const auto& activity : *activities)
    result_arr.push_back(activity->ConvertToExtensionActivity());

  // Populate the return object.
  ActivityResultSet result_set;
  result_set.activities = std::move(result_arr);
  Respond(ArgumentList(
      activity_log_private::GetExtensionActivities::Results::Create(
          result_set)));
}

ExtensionFunction::ResponseAction
ActivityLogPrivateDeleteActivitiesFunction::Run() {
  std::unique_ptr<activity_log_private::DeleteActivities::Params> params(
      activity_log_private::DeleteActivities::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  // Put the arguments in the right format.
  std::vector<int64_t> action_ids;
  int64_t value;
  for (size_t i = 0; i < params->activity_ids.size(); i++) {
    if (base::StringToInt64(params->activity_ids[i], &value))
      action_ids.push_back(value);
  }

  ActivityLog* activity_log = ActivityLog::GetInstance(browser_context());
  DCHECK(activity_log);
  activity_log->RemoveActions(action_ids);
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction
ActivityLogPrivateDeleteActivitiesByExtensionFunction::Run() {
  std::unique_ptr<activity_log_private::DeleteActivitiesByExtension::Params>
      params(activity_log_private::DeleteActivitiesByExtension::Params::Create(
          *args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  ActivityLog* activity_log = ActivityLog::GetInstance(browser_context());
  DCHECK(activity_log);
  activity_log->RemoveExtensionData(params->extension_id);
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction
ActivityLogPrivateDeleteDatabaseFunction::Run() {
  ActivityLog* activity_log = ActivityLog::GetInstance(browser_context());
  DCHECK(activity_log);
  activity_log->DeleteDatabase();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActivityLogPrivateDeleteUrlsFunction::Run() {
  std::unique_ptr<activity_log_private::DeleteUrls::Params> params(
      activity_log_private::DeleteUrls::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  // Put the arguments in the right format.
  std::vector<GURL> gurls;
  const std::vector<std::string>& urls = *params->urls;
  for (const std::string& url : urls)
    gurls.push_back(GURL(url));

  ActivityLog* activity_log = ActivityLog::GetInstance(browser_context());
  DCHECK(activity_log);
  activity_log->RemoveURLs(gurls);
  return RespondNow(NoArguments());
}

}  // namespace extensions
