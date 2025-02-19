// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/persistence/site_data/site_data_cache_facade.h"

#include "base/auto_reset.h"
#include "base/callback.h"
#include "base/run_loop.h"
#include "base/task/post_task.h"
#include "base/test/bind_test_util.h"
#include "chrome/browser/performance_manager/persistence/site_data/leveldb_site_data_store.h"
#include "chrome/browser/performance_manager/persistence/site_data/site_data_cache_factory.h"
#include "chrome/browser/performance_manager/persistence/site_data/site_data_cache_impl.h"
#include "chrome/browser/performance_manager/persistence/site_data/unittest_utils.h"
#include "chrome/test/base/testing_profile.h"
#include "components/performance_manager/performance_manager_impl.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace performance_manager {

namespace {

// TODO(https://crbug.com/1042727): Fix test GURL scoping and remove this getter
// function.
url::Origin TestOrigin() {
  return url::Origin::Create(GURL("http://www.a.com"));
}
url::Origin TestOrigin2() {
  return url::Origin::Create(GURL("http://www.b.com"));
}

// Mock version of a SiteDataCacheImpl. In practice instances of this object
// live on the Performance Manager sequence and all the mocked methods will be
// called from there.
class LenienMockSiteDataCacheImpl : public SiteDataCacheImpl {
 public:
  explicit LenienMockSiteDataCacheImpl(const std::string& browser_context_id)
      : SiteDataCacheImpl(browser_context_id) {}
  ~LenienMockSiteDataCacheImpl() override = default;

  // The 2 following functions allow setting the expectations for the mocked
  // functions. Any call to one of these functions should be followed by the
  // call that will caused the mocked the function to be called and then by a
  // call to |WaitForExpectations|. Only one expectation can be set at a time.

  void SetClearSiteDataForOriginsExpectations(
      const std::vector<url::Origin>& expected_origins) {
    ASSERT_FALSE(run_loop_);
    run_loop_ = std::make_unique<base::RunLoop>();
    auto quit_closure = run_loop_->QuitClosure();
    EXPECT_CALL(*this, ClearSiteDataForOrigins(::testing::Eq(expected_origins)))
        .WillOnce(
            ::testing::InvokeWithoutArgs([closure = std::move(quit_closure)]() {
              std::move(closure).Run();
            }));
  }

  void SetClearAllSiteDataExpectations() {
    ASSERT_FALSE(run_loop_);
    run_loop_ = std::make_unique<base::RunLoop>();
    auto quit_closure = run_loop_->QuitClosure();
    EXPECT_CALL(*this, ClearAllSiteData())
        .WillOnce(
            ::testing::InvokeWithoutArgs([closure = std::move(quit_closure)]() {
              std::move(closure).Run();
            }));
  }

  void WaitForExpectations() {
    ASSERT_TRUE(run_loop_);
    run_loop_->Run();
    run_loop_.reset();
    ::testing::Mock::VerifyAndClear(this);
  }

 private:
  MOCK_METHOD1(ClearSiteDataForOrigins, void(const std::vector<url::Origin>&));
  MOCK_METHOD0(ClearAllSiteData, void());

  std::unique_ptr<base::RunLoop> run_loop_;

  DISALLOW_COPY_AND_ASSIGN(LenienMockSiteDataCacheImpl);
};
using MockSiteDataCache = ::testing::StrictMock<LenienMockSiteDataCacheImpl>;

class SiteDataCacheFacadeTest : public testing::TestWithPerformanceManager {
 public:
  SiteDataCacheFacadeTest() = default;
  ~SiteDataCacheFacadeTest() override = default;

  void SetUp() override {
    testing::TestWithPerformanceManager::SetUp();
    PerformanceManagerImpl::GetInstance()->CallOnGraphImpl(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<SiteDataCacheFactory> site_data_cache_factory,
               performance_manager::GraphImpl* graph) {
              graph->PassToGraph(std::move(site_data_cache_factory));
            },
            std::make_unique<SiteDataCacheFactory>()));
    use_in_memory_db_for_testing_ =
        LevelDBSiteDataStore::UseInMemoryDBForTesting();
    profile_ = std::make_unique<TestingProfile>();
  }

  void TearDown() override {
    use_in_memory_db_for_testing_.reset();
    profile_.reset();
    testing::TestWithPerformanceManager::TearDown();
  }

  TestingProfile* profile() { return profile_.get(); }

  // Replace the SiteDataCache associated with |profile_| with a mock one.
  MockSiteDataCache* SetUpMockCache() {
    MockSiteDataCache* mock_cache_raw = nullptr;
    base::RunLoop run_loop;
    auto quit_closure = run_loop.QuitClosure();
    auto browser_context_id = profile()->UniqueId();
    PerformanceManagerImpl::GetInstance()->CallOnGraphImpl(
        FROM_HERE,
        base::BindLambdaForTesting([&](performance_manager::GraphImpl* graph) {
          auto mock_cache =
              std::make_unique<MockSiteDataCache>(browser_context_id);
          mock_cache_raw = mock_cache.get();

          SiteDataCacheFactory::GetInstance()->ReplaceCacheForTesting(
              browser_context_id, std::move(mock_cache));
          std::move(quit_closure).Run();
        }));
    run_loop.Run();
    return mock_cache_raw;
  }

 private:
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<base::AutoReset<bool>> use_in_memory_db_for_testing_;
};

}  // namespace

TEST_F(SiteDataCacheFacadeTest, IsDataCacheRecordingForTesting) {
  bool cache_is_recording = false;

  SiteDataCacheFacade data_cache_facade(profile());
  data_cache_facade.WaitUntilCacheInitializedForTesting();
  {
    base::RunLoop run_loop;
    auto quit_closure = run_loop.QuitClosure();
    data_cache_facade.IsDataCacheRecordingForTesting(
        base::BindLambdaForTesting([&](bool is_recording) {
          cache_is_recording = is_recording;
          std::move(quit_closure).Run();
        }));
    run_loop.Run();
  }
  EXPECT_TRUE(cache_is_recording);

  SiteDataCacheFacade off_record_data_cache_facade(
      profile()->GetOffTheRecordProfile());
  {
    base::RunLoop run_loop;
    auto quit_closure = run_loop.QuitClosure();
    off_record_data_cache_facade.IsDataCacheRecordingForTesting(
        base::BindLambdaForTesting([&](bool is_recording) {
          cache_is_recording = is_recording;
          quit_closure.Run();
        }));
    run_loop.Run();
  }

  EXPECT_FALSE(cache_is_recording);
}

// Verify that an origin is removed from the data cache (in memory and on disk)
// when there are no more references to it in the history, after the history is
// partially cleared.
TEST_F(SiteDataCacheFacadeTest, OnURLsDeleted_Partial_OriginNotReferenced) {
  history::URLRows urls_to_delete = {history::URLRow(TestOrigin().GetURL()),
                                     history::URLRow(TestOrigin2().GetURL())};
  history::DeletionInfo deletion_info =
      history::DeletionInfo::ForUrls(urls_to_delete, std::set<GURL>());
  deletion_info.set_deleted_urls_origin_map({
      {TestOrigin().GetURL(), {0, base::Time::Now()}},
      {TestOrigin2().GetURL(), {0, base::Time::Now()}},
  });

  SiteDataCacheFacade data_cache_facade(profile());
  data_cache_facade.WaitUntilCacheInitializedForTesting();

  auto* mock_cache_raw = SetUpMockCache();
  mock_cache_raw->SetClearSiteDataForOriginsExpectations(
      {TestOrigin(), TestOrigin2()});
  data_cache_facade.OnURLsDeleted(nullptr, deletion_info);
  mock_cache_raw->WaitForExpectations();
}

// Verify that an origin is *not* removed from the data cache (in memory and on
// disk) when there remain references to it in the history, after the history is
// partially cleared.
TEST_F(SiteDataCacheFacadeTest, OnURLsDeleted_Partial_OriginStillReferenced) {
  history::URLRows urls_to_delete = {history::URLRow(TestOrigin().GetURL()),
                                     history::URLRow(TestOrigin2().GetURL())};
  history::DeletionInfo deletion_info =
      history::DeletionInfo::ForUrls(urls_to_delete, std::set<GURL>());
  deletion_info.set_deleted_urls_origin_map({
      {TestOrigin().GetURL(), {0, base::Time::Now()}},
      {TestOrigin2().GetURL(), {3, base::Time::Now()}},
  });

  SiteDataCacheFacade data_cache_facade(profile());
  data_cache_facade.WaitUntilCacheInitializedForTesting();

  auto* mock_cache_raw = SetUpMockCache();
  // |TestOrigin2()| shouldn't be removed as there's still some references to it
  // in the history.
  mock_cache_raw->SetClearSiteDataForOriginsExpectations({TestOrigin()});
  data_cache_facade.OnURLsDeleted(nullptr, deletion_info);
  mock_cache_raw->WaitForExpectations();
}

// Verify that origins are removed from the data cache (in memory and on disk)
// when the history is completely cleared.
TEST_F(SiteDataCacheFacadeTest, OnURLsDeleted_Full) {
  SiteDataCacheFacade data_cache_facade(profile());
  data_cache_facade.WaitUntilCacheInitializedForTesting();

  auto* mock_cache_raw = SetUpMockCache();
  mock_cache_raw->SetClearAllSiteDataExpectations();
  data_cache_facade.OnURLsDeleted(nullptr,
                                  history::DeletionInfo::ForAllHistory());
  mock_cache_raw->WaitForExpectations();
}

}  // namespace performance_manager
