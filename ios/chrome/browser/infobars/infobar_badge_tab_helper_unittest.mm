// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/infobars/infobar_badge_tab_helper.h"

#include "base/test/scoped_feature_list.h"
#include "components/infobars/core/infobar_feature.h"
#include "ios/chrome/browser/infobars/infobar_badge_tab_helper.h"
#include "ios/chrome/browser/infobars/infobar_manager_impl.h"
#import "ios/chrome/browser/infobars/test/fake_infobar_badge_tab_helper_delegate.h"
#import "ios/chrome/browser/infobars/test/fake_infobar_ios.h"
#import "ios/chrome/browser/ui/badges/badge_item.h"
#include "ios/chrome/browser/ui/badges/badge_type_util.h"
#import "ios/chrome/browser/ui/infobars/test/fake_infobar_ui_delegate.h"
#import "ios/web/public/test/fakes/test_navigation_manager.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "testing/platform_test.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// The InfobarTypes to use for the test.
const InfobarType kInfobarTypeWithBadge = InfobarType::kInfobarTypePasswordSave;
const InfobarType kInfobarTypeNoBadge = InfobarType::kInfobarTypeConfirm;
}  // namespace

// Test fixture for testing InfobarBadgeTabHelper.
class InfobarBadgeTabHelperTest : public PlatformTest {
 protected:
  InfobarBadgeTabHelperTest()
      : delegate_([[FakeInfobarTabHelperDelegate alloc] init]) {
    // Enable kIOSInfobarUIReboot flag.
    feature_list_.InitAndEnableFeature(kIOSInfobarUIReboot);

    // Setup navigation manager. Needed for InfobarManager.
    web_state_.SetNavigationManager(
        std::make_unique<web::TestNavigationManager>());

    // Create the InfobarManager for web_state_.
    InfoBarManagerImpl::CreateForWebState(&web_state_);

    // Create the InfobarBadgeTabHelper for web_state_ and set its delegate.
    InfobarBadgeTabHelper::CreateForWebState(&web_state_);
    tab_helper()->SetDelegate(delegate_);
  }

  // Adds a FakeInfobarIOS with specified badge support to the WebState's
  // InfoBarManagerImpl.  Set replace_existing to true, if a matching infobar
  // (same message_text) should be replaced with new one instead of being
  // ignored. Returns the added infobar.
  FakeInfobarIOS* AddInfobar(bool has_badge, bool replace_existing = false) {
    std::unique_ptr<FakeInfobarIOS> added_infobar =
        std::make_unique<FakeInfobarIOS>();
    added_infobar->fake_ui_delegate().infobarType =
        has_badge ? kInfobarTypeWithBadge : kInfobarTypeNoBadge;
    added_infobar->fake_ui_delegate().hasBadge = has_badge;
    FakeInfobarIOS* infobar = added_infobar.get();
    InfoBarManagerImpl::FromWebState(&web_state_)
        ->AddInfoBar(std::move(added_infobar), replace_existing);
    return infobar;
  }

  // Returns InfobarBadgeTabHelper attached to web_state_.
  InfobarBadgeTabHelper* tab_helper() {
    return InfobarBadgeTabHelper::FromWebState(&web_state_);
  }

  base::test::ScopedFeatureList feature_list_;
  web::TestWebState web_state_;
  FakeInfobarTabHelperDelegate* delegate_ = nil;
};

// Test the badge state after changes to the state of an Infobar. Infobar badges
// should always be tappable.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeState) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item.
  FakeInfobarIOS* infobar = AddInfobar(/*has_badge=*/true);
  InfobarType added_type = infobar->fake_ui_delegate().infobarType;
  // Simulate presenting the banner UI and verify that the badge state is sent
  // to the delegate.
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  id<BadgeItem> item =
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)];
  EXPECT_TRUE(item);
  EXPECT_TRUE(item.tappable);
  EXPECT_TRUE(item.badgeState & BadgeStatePresented);
  // Simulate accepting the infobar and verify that the badge state is udpated.
  infobar->set_accepted(true);
  EXPECT_TRUE(item.tappable);
  EXPECT_TRUE(item.badgeState & BadgeStateAccepted);
  EXPECT_TRUE(item.badgeState & BadgeStateRead);
  // Simulate reverting the infobar and verify that the badge state is udpated.
  infobar->set_accepted(false);
  EXPECT_FALSE(item.badgeState & BadgeStateAccepted);
}

// Test the badge state after changes to the state of an Infobar. Infobar badges
// should always be tappable.  Uses depreated API.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeStateDeprecated) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item.
  InfobarType added_type =
      AddInfobar(/*has_badge=*/true)->fake_ui_delegate().infobarType;
  // Simulate presenting the banner UI and verify that the badge state is sent
  // to the delegate.
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  id<BadgeItem> item =
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)];
  EXPECT_TRUE(item);
  EXPECT_TRUE(item.tappable);
  EXPECT_TRUE(item.badgeState & BadgeStatePresented);
  // Simulate accepting the infobar and verify that the badge state is udpated.
  tab_helper()->UpdateBadgeForInfobarAccepted(added_type);
  EXPECT_TRUE(item.tappable);
  EXPECT_TRUE(item.badgeState & BadgeStateAccepted);
  EXPECT_TRUE(item.badgeState & BadgeStateRead);
  // Simulate reverting the infobar and verify that the badge state is udpated.
  tab_helper()->UpdateBadgeForInfobarReverted(added_type);
  EXPECT_FALSE(item.badgeState & BadgeStateAccepted);
}

// Tests that adding an infobar that doesn't support badges does not notify the
// delegate of BadgeItem creation.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeStateNoBadge) {
  InfobarType added_type =
      AddInfobar(/*has_badge=*/false)->fake_ui_delegate().infobarType;
  EXPECT_FALSE(
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)]);
}

// Tests that the InfobarBadge has not been removed after dismissing the
// InfobarBanner.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeOnBannerDismissal) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item, then simulate presentation and dismissal.
  InfobarType added_type =
      AddInfobar(/*has_badge=*/true)->fake_ui_delegate().infobarType;
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  tab_helper()->UpdateBadgeForInfobarBannerDismissed(added_type);
  // Verify that the BadgeItem was not removed and that its state is dismissed.
  id<BadgeItem> item =
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)];
  EXPECT_TRUE(item);
  EXPECT_FALSE(item.badgeState & BadgeStatePresented);
}

// Test that the Accepted badge state remains after dismissing the
// InfobarBanner.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeOnBannerAccepted) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item, then simulate presentation, acceptance, and dismissal.
  FakeInfobarIOS* infobar = AddInfobar(/*has_badge=*/true);
  InfobarType added_type = infobar->fake_ui_delegate().infobarType;
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  infobar->set_accepted(true);
  tab_helper()->UpdateBadgeForInfobarBannerDismissed(added_type);
  // Verify that the BadgeItem was not removed and that its state is dismissed.
  id<BadgeItem> item =
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)];
  EXPECT_TRUE(item);
  EXPECT_TRUE(item.badgeState & BadgeStateAccepted);
}

// Test that the Accepted badge state remains after dismissing the
// InfobarBanner.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeOnBannerAcceptedDeprecated) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item, then simulate presentation, acceptance, and dismissal.
  InfobarType added_type =
      AddInfobar(/*has_badge=*/true)->fake_ui_delegate().infobarType;
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  tab_helper()->UpdateBadgeForInfobarAccepted(added_type);
  tab_helper()->UpdateBadgeForInfobarBannerDismissed(added_type);
  // Verify that the BadgeItem was not removed and that its state is dismissed.
  id<BadgeItem> item =
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)];
  EXPECT_TRUE(item);
  EXPECT_TRUE(item.badgeState & BadgeStateAccepted);
}

// Test that destroying the InfobarView stops displaying the badge.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarBadgeOnInfobarDestruction) {
  // Add a badge-supporting infobar to the InfobarManager to create the badge
  // item, then simulate presentation and dismissal.
  FakeInfobarIOS* added_infobar = AddInfobar(/*has_badge=*/true);
  InfobarType added_type = added_infobar->fake_ui_delegate().infobarType;
  tab_helper()->UpdateBadgeForInfobarBannerPresented(added_type);
  tab_helper()->UpdateBadgeForInfobarBannerDismissed(added_type);
  ASSERT_TRUE([delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)]);
  // Remove the infobar from the manager and verify that the BadgeItem is also
  // removed.
  InfoBarManagerImpl::FromWebState(&web_state_)->RemoveInfoBar(added_infobar);
  EXPECT_FALSE(
      [delegate_ itemForBadgeType:BadgeTypeForInfobarType(added_type)]);
}

// Test that replacing infobar, doesn't crash.
TEST_F(InfobarBadgeTabHelperTest, TestInfobarReplacing) {
  // Test tab helper by driving it through InfoBarManager.
  AddInfobar(/*has_badge=*/true);
  // Check first one added correctly.
  EXPECT_TRUE(InfoBarManagerImpl::FromWebState(&web_state_)->infobar_count() ==
              1);
  // Replace with second one.
  FakeInfobarIOS* infobar2 =
      AddInfobar(/*has_badge=*/true, /*replace_existing=*/true);
  // Should be only one.
  EXPECT_TRUE(InfoBarManagerImpl::FromWebState(&web_state_)->infobar_count() ==
              1);
  // If first one wasn't replaced this will fail.
  InfoBarManagerImpl::FromWebState(&web_state_)->RemoveInfoBar(infobar2);
  // Left with none.
  EXPECT_TRUE(InfoBarManagerImpl::FromWebState(&web_state_)->infobar_count() ==
              0);
  // No crash.
}
