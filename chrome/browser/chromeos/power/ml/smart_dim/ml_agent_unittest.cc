// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/power/ml/smart_dim/ml_agent.h"

#include <memory>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/dbus/machine_learning/machine_learning_client.h"
#include "chromeos/services/machine_learning/public/cpp/fake_service_connection.h"
#include "chromeos/services/machine_learning/public/cpp/service_connection.h"
#include "chromeos/test/chromeos_test_utils.h"
#include "components/assist_ranker/proto/example_preprocessor.pb.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace power {
namespace ml {
namespace {

// Arbitrary inactivity score for the fake ml service connection to return, and
// its quantization via sigmoid transform:
constexpr double kTestInactivityScore = -3.7;
constexpr int kQuantizedTestInactivityScore = 2;

// Quantization of k20190521ModelDefaultDimThreshold (-0.6), the builtin
// threshold for SmartDimModelV3, via sigmoid.
// It's higher than kTestInactivityScore , which implies a no dim decision.
constexpr int kQuantizedBuiltinThreshold = 35;

// Arbitrary dim thresholds lower than kTestInactivityScore and its quantization
// via sigmoid transform, implying a yes dim decisions.
constexpr double kLowDimThreshold = -10.0;
constexpr int kQuantizedLowDimThreshold = 0;

// Test data lies in src/chromeos/test/data/smart_dim.
base::FilePath GetTestDataPath(const std::string& file_name) {
  base::FilePath path;
  CHECK(chromeos::test_utils::GetTestDataPath("smart_dim", file_name, &path));
  return path;
}

void LoadDownloadableSmartDimComponent(const double& threshold) {
  const char json_string_template[] =
      "{"
      "\"input_names\": [\"x\", \"y\"],"
      "\"input_nodes\": [3, 4],"
      "\"output_names\": [\"z\"],"
      "\"output_nodes\": [5],"
      "\"threshold\": %f,"
      "\"expected_feature_size\": 343,"
      "\"metrics_model_name\": \"smart_dim_model\""
      "}";
  const std::string json_string =
      base::StringPrintf(json_string_template, threshold);

  const std::string model_string = "This is a model string";

  std::string pb_string;
  const base::FilePath pb_path =
      GetTestDataPath("20181115_example_preprocessor_config.pb");
  CHECK(base::ReadFileToString(pb_path, &pb_string));

  SmartDimMlAgent::GetInstance()->OnComponentReady(json_string, pb_string,
                                                   model_string);
}

UserActivityEvent::Features DefaultFeatures() {
  UserActivityEvent::Features features;
  // Bucketize to 95.
  features.set_battery_percent(96.0);
  features.set_device_management(UserActivityEvent::Features::UNMANAGED);
  features.set_device_mode(UserActivityEvent::Features::CLAMSHELL);
  features.set_device_type(UserActivityEvent::Features::CHROMEBOOK);
  // Bucketize to 200.
  features.set_key_events_in_last_hour(290);
  features.set_last_activity_day(UserActivityEvent::Features::THU);
  // Bucketize to 7.
  features.set_last_activity_time_sec(25920);
  // Bucketize to 7.
  features.set_last_user_activity_time_sec(25920);
  // Bucketize to 2000.
  features.set_mouse_events_in_last_hour(2600);
  features.set_on_battery(false);
  features.set_previous_negative_actions_count(3);
  features.set_previous_positive_actions_count(0);
  features.set_recent_time_active_sec(190);
  features.set_video_playing_time_sec(0);
  features.set_on_to_dim_sec(30);
  features.set_dim_to_screen_off_sec(10);
  features.set_time_since_last_key_sec(30);
  features.set_time_since_last_mouse_sec(688);
  // Bucketize to 900.
  features.set_time_since_video_ended_sec(1100);
  features.set_has_form_entry(false);
  features.set_source_id(123);  // not used.
  features.set_engagement_score(40);
  features.set_tab_domain("//mail.google.com");
  return features;
}

}  // namespace

class SmartDimMlAgentTest : public testing::Test {
 public:
  SmartDimMlAgentTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED) {}

  void SetUp() override {
    MachineLearningClient::InitializeFake();
    machine_learning::ServiceConnection::UseFakeServiceConnectionForTesting(
        &fake_service_connection_);
    fake_service_connection_.SetOutputValue(
        std::vector<int64_t>{1L}, std::vector<double>{kTestInactivityScore});
  }

  void TearDown() override { MachineLearningClient::Shutdown(); }

 protected:
  machine_learning::FakeServiceConnectionImpl fake_service_connection_;
  // DownloadWorker::InitializeFromComponent posts task to BrowserThread::UI,
  // while content::BrowserTaskEnvironment provides BrowserThread support in
  // unittest.
  content::BrowserTaskEnvironment task_environment_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SmartDimMlAgentTest);
};

// This test covers two things:
// 1. ml_agent can swap between download worker and builtin worker as per
// IsDownloadWorkerReady.
// 2. ml_agent can combine results from worker with threshold to get right
// DIM/NO_DIM decisions.
TEST_F(SmartDimMlAgentTest, SwitchBetweenWorkers) {
  auto* agent = SmartDimMlAgent::GetInstance();
  agent->ResetForTesting();
  base::test::ScopedFeatureList feature_list;
  // Enable kSmartDimModelV3, this makes builtin worker use
  // k20190521ModelDefaultDimThreshold, a relatively high threshold.
  feature_list.InitAndEnableFeature(features::kSmartDimModelV3);

  // Without LoadDownloadableSmartDimComponent, download_worker_ is not ready.
  EXPECT_FALSE(agent->IsDownloadWorkerReady());

  bool callback_done = false;
  // By checking prediction.decision_threshold == kQuantizedBuiltinThreshold we
  // know that builtin worker is at work. This threshold is high, so the
  // decision is NO_DIM.
  agent->RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               EXPECT_EQ(
                                   UserActivityEvent::ModelPrediction::NO_DIM,
                                   prediction.response());
                               EXPECT_EQ(kQuantizedBuiltinThreshold,
                                         prediction.decision_threshold());
                               EXPECT_EQ(kQuantizedTestInactivityScore,
                                         prediction.inactivity_score());
                               *callback_done = true;
                             },
                             &callback_done));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);

  // After load from download components, it should use download worker.
  LoadDownloadableSmartDimComponent(kLowDimThreshold);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(agent->IsDownloadWorkerReady());

  callback_done = false;
  // By checking prediction.decision_threshold == kQuantizedLowDimThreshold we
  // know that download worker is at work. This threshold is low, so the
  // decision is DIM.
  agent->RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               EXPECT_EQ(
                                   UserActivityEvent::ModelPrediction::DIM,
                                   prediction.response());
                               EXPECT_EQ(kQuantizedLowDimThreshold,
                                         prediction.decision_threshold());
                               EXPECT_EQ(kQuantizedTestInactivityScore,
                                         prediction.inactivity_score());
                               *callback_done = true;
                             },
                             &callback_done));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Check that CancelableCallback ensures a callback doesn't execute twice, in
// case two RequestDimDecision() calls were made before any callback ran.
TEST_F(SmartDimMlAgentTest, CheckCancelableCallback) {
  SmartDimMlAgent::GetInstance()->ResetForTesting();
  LoadDownloadableSmartDimComponent(kLowDimThreshold);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(SmartDimMlAgent::GetInstance()->IsDownloadWorkerReady());

  bool callback_done = false;
  int num_callbacks_run = 0;
  for (int i = 0; i < 2; i++) {
    SmartDimMlAgent::GetInstance()->RequestDimDecision(
        DefaultFeatures(),
        base::BindOnce(
            [](bool* callback_done, int* num_callbacks_run,
               UserActivityEvent::ModelPrediction prediction) {
              EXPECT_EQ(UserActivityEvent::ModelPrediction::DIM,
                        prediction.response());
              EXPECT_EQ(kQuantizedLowDimThreshold,
                        prediction.decision_threshold());
              EXPECT_EQ(kQuantizedTestInactivityScore,
                        prediction.inactivity_score());
              *callback_done = true;
              (*num_callbacks_run)++;
            },
            &callback_done, &num_callbacks_run));
  }
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);
  EXPECT_EQ(1, num_callbacks_run);
}

// Check that CancelPreviousRequest() can successfully prevent a previous
// requested dim decision request from running.
TEST_F(SmartDimMlAgentTest, CheckCanceledRequest) {
  SmartDimMlAgent::GetInstance()->ResetForTesting();
  LoadDownloadableSmartDimComponent(kLowDimThreshold);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(SmartDimMlAgent::GetInstance()->IsDownloadWorkerReady());

  bool callback_done = false;
  SmartDimMlAgent::GetInstance()->RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               *callback_done = true;
                             },
                             &callback_done));
  SmartDimMlAgent::GetInstance()->CancelPreviousRequest();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(callback_done);
}

}  // namespace ml
}  // namespace power
}  // namespace chromeos
