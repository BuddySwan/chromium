// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/power/ml/smart_dim/ml_agent_util.h"

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "chromeos/test/chromeos_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace power {
namespace ml {

namespace {
std::string ReadMetadataFromTestData(const std::string& filename) {
  base::FilePath json_path;
  std::string json_string;

  CHECK(
      chromeos::test_utils::GetTestDataPath("smart_dim", filename, &json_path));
  CHECK(base::ReadFileToString(json_path, &json_string));
  return json_string;
}
}  // namespace

TEST(SmartDimMlAgentUtilTest, ParseInvalidMetadata) {
  const std::string json_string =
      ReadMetadataFromTestData("invalid_model_metadata.json");
  ASSERT_TRUE(json_string.size());

  std::string metrics_model_name;
  double threshold;
  size_t expected_feature_size;
  base::flat_map<std::string, int> inputs;
  base::flat_map<std::string, int> outputs;

  EXPECT_FALSE(ParseMetaInfoFromString(json_string, &metrics_model_name,
                                       &threshold, &expected_feature_size,
                                       &inputs, &outputs));
  EXPECT_EQ(expected_feature_size, 0LU);
  EXPECT_EQ(metrics_model_name, "");
  EXPECT_DOUBLE_EQ(threshold, 0.0);
  EXPECT_EQ(inputs.size(), 0LU);
  EXPECT_EQ(outputs.size(), 0LU);
}

TEST(SmartDimMlAgentUtilTest, ParseValidMetadata) {
  const std::string json_string =
      ReadMetadataFromTestData("valid_model_metadata.json");
  ASSERT_TRUE(json_string.size());

  std::string metrics_model_name;
  double threshold;
  size_t expected_feature_size;
  base::flat_map<std::string, int> inputs;
  base::flat_map<std::string, int> outputs;

  EXPECT_TRUE(ParseMetaInfoFromString(json_string, &metrics_model_name,
                                      &threshold, &expected_feature_size,
                                      &inputs, &outputs));
  EXPECT_EQ(expected_feature_size, 343LU);
  EXPECT_EQ(metrics_model_name, "smart_dim_model");
  EXPECT_DOUBLE_EQ(threshold, 0.7);
  EXPECT_EQ(inputs, (base::flat_map<std::string, int>{{"x", 3}, {"y", 4}}));
  EXPECT_EQ(outputs, (base::flat_map<std::string, int>{{"z", 5}}));
}

}  // namespace ml
}  // namespace power
}  // namespace chromeos
