// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/ios/ios_util.h"
#include "base/strings/sys_string_conversions.h"
#import "base/test/ios/wait_util.h"
#import "ios/chrome/browser/ui/autofill/autofill_app_interface.h"
#import "ios/chrome/browser/ui/settings/autofill/features.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/earl_grey/chrome_actions.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_test_case.h"
#import "ios/testing/earl_grey/app_launch_manager.h"
#import "ios/testing/earl_grey/earl_grey_test.h"
#import "ios/testing/earl_grey/keyboard_app_interface.h"
#include "ios/web/public/test/element_selector.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using base::test::ios::kWaitForActionTimeout;
using chrome_test_util::CancelButton;
using chrome_test_util::ManualFallbackAddCreditCardsMatcher;
using chrome_test_util::ManualFallbackCreditCardIconMatcher;
using chrome_test_util::ManualFallbackCreditCardTableViewMatcher;
using chrome_test_util::ManualFallbackCreditCardTableViewWindowMatcher;
using chrome_test_util::ManualFallbackFormSuggestionViewMatcher;
using chrome_test_util::ManualFallbackKeyboardIconMatcher;
using chrome_test_util::ManualFallbackManageCreditCardsMatcher;
using chrome_test_util::NavigationBarCancelButton;
using chrome_test_util::SettingsCreditCardMatcher;
using chrome_test_util::StaticTextWithAccessibilityLabelId;
using chrome_test_util::TapWebElementWithId;

namespace {

const char kFormElementUsername[] = "username";
const char kFormElementOtherStuff[] = "otherstuff";

NSString* kLocalCardNumber = @"4111111111111111";
NSString* kLocalCardHolder = @"Test User";
NSString* kLocalCardExpirationMonth = @"11";
NSString* kLocalCardExpirationYear = @"22";

// Unicode characters used in card number:
//  - 0x0020 - Space.
//  - 0x2060 - WORD-JOINER (makes string undivisible).
constexpr base::char16 separator[] = {0x2060, 0x0020, 0};
constexpr base::char16 kMidlineEllipsis[] = {
    0x2022, 0x2060, 0x2006, 0x2060, 0x2022, 0x2060, 0x2006, 0x2060, 0x2022,
    0x2060, 0x2006, 0x2060, 0x2022, 0x2060, 0x2006, 0x2060, 0};
NSString* kObfuscatedNumberPrefix = base::SysUTF16ToNSString(
    kMidlineEllipsis + base::string16(separator) + kMidlineEllipsis +
    base::string16(separator) + kMidlineEllipsis + base::string16(separator));

NSString* kLocalNumberObfuscated =
    [NSString stringWithFormat:@"%@1111", kObfuscatedNumberPrefix];

NSString* kServerNumberObfuscated =
    [NSString stringWithFormat:@"%@2109", kObfuscatedNumberPrefix];

const char kFormHTMLFile[] = "/multi_field_form.html";

// Matcher for the not secure website alert.
id<GREYMatcher> NotSecureWebsiteAlert() {
  return StaticTextWithAccessibilityLabelId(
      IDS_IOS_MANUAL_FALLBACK_NOT_SECURE_TITLE);
}

// Polls the JavaScript query |java_script_condition| until the returned
// |boolValue| is YES with a kWaitForActionTimeout timeout.
BOOL WaitForJavaScriptCondition(NSString* java_script_condition) {
  auto verify_block = ^BOOL {
    id value = [ChromeEarlGrey executeJavaScript:java_script_condition];
    return [value isEqual:@YES];
  };
  NSTimeInterval timeout = base::test::ios::kWaitForActionTimeout;
  NSString* condition_name = [NSString
      stringWithFormat:@"Wait for JS condition: %@", java_script_condition];
  GREYCondition* condition = [GREYCondition conditionWithName:condition_name
                                                        block:verify_block];
  return [condition waitWithTimeout:timeout];
}

// Waits for the keyboard to appear. Returns NO on timeout.
BOOL WaitForKeyboardToAppear() {
  GREYCondition* waitForKeyboard = [GREYCondition
      conditionWithName:@"Wait for keyboard"
                  block:^BOOL {
                    return [ChromeEarlGrey isKeyboardShownWithError:nil];
                  }];
  return [waitForKeyboard waitWithTimeout:kWaitForActionTimeout];
}

}  // namespace

// Integration Tests for Manual Fallback credit cards View Controller.
@interface CreditCardViewControllerTestCase : ChromeTestCase
@end

@implementation CreditCardViewControllerTestCase

- (void)launchAppForTestMethod {
  [[AppLaunchManager sharedManager]
      ensureAppLaunchedWithFeaturesEnabled:{kCreditCardScanner}
                                  disabled:{}
                            relaunchPolicy:NoForceRelaunchAndResetState];
}

- (void)setUp {
  [super setUp];
  GREYAssertTrue(self.testServer->Start(), @"Test server failed to start.");
  const GURL URL = self.testServer->GetURL(kFormHTMLFile);
  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGrey waitForWebStateContainingText:"hello!"];
  [AutofillAppInterface clearCreditCardStore];
}

- (void)tearDown {
  [AutofillAppInterface clearCreditCardStore];
  [ChromeEarlGrey rotateDeviceToOrientation:UIDeviceOrientationPortrait
                                      error:nil];
  [super tearDown];
}

#pragma mark - Tests

// Tests that the credit card view butotn is absent when there are no cards
// available.
- (void)testCreditCardsButtonAbsentWhenNoCreditCardsAvailable {
  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Verify there's no credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that the credit card view controller appears on screen.
- (void)testCreditCardsViewControllerIsPresented {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit cards controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];
}

// Tests that the credit cards view controller contains the "Manage Credit
// Cards..." action.
- (void)testCreditCardsViewControllerContainsManageCreditCardsAction {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Verify the credit cards controller contains the "Manage Credit Cards..."
  // action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackManageCreditCardsMatcher()]
      assertWithMatcher:grey_interactable()];
}

// Tests that the "Manage Credit Cards..." action works.
- (void)testManageCreditCardsActionOpensCreditCardSettings {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Manage Credit Cards..." action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackManageCreditCardsMatcher()]
      performAction:grey_tap()];

  // Verify the credit cards settings opened.
  [[EarlGrey selectElementWithMatcher:SettingsCreditCardMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];
}

// Tests that the manual fallback view and icon is not highlighted after
// presenting the manage credit cards view.
- (void)testCreditCardsStateAfterPresentingCreditCardSettings {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Scroll to the right.
  [[EarlGrey selectElementWithMatcher:ManualFallbackFormSuggestionViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeRight)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the status of the icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_not(grey_userInteractionEnabled())];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Manage Credit Cards..." action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackManageCreditCardsMatcher()]
      performAction:grey_tap()];

  // Tap Cancel Button.
  [[EarlGrey selectElementWithMatcher:NavigationBarCancelButton()]
      performAction:grey_tap()];

  // Verify the status of the icons.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_userInteractionEnabled()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      assertWithMatcher:grey_not(grey_sufficientlyVisible())];

  // Verify the keyboard is not cover by the cards view.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that the "Add Credit Cards..." action works.
- (void)testAddCreditCardsActionOpensAddCreditCardSettings {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Add Credit Cards..." action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackAddCreditCardsMatcher()]
      performAction:grey_tap()];

  // Verify the credit cards settings opened.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::AddCreditCardView()]
      assertWithMatcher:grey_sufficientlyVisible()];
}

// Tests that the "Add Credit Cards..." action works on OTR.
- (void)testOTRAddCreditCardsActionOpensAddCreditCardSettings {
  [AutofillAppInterface saveLocalCreditCard];

  // Open a tab in incognito.
  [ChromeEarlGrey openNewIncognitoTab];
  const GURL URL = self.testServer->GetURL(kFormHTMLFile);
  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGrey waitForWebStateContainingText:"hello!"];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Add Credit Cards..." action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackAddCreditCardsMatcher()]
      performAction:grey_tap()];

  // Verify the credit cards settings opened.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::AddCreditCardView()]
      assertWithMatcher:grey_sufficientlyVisible()];
}

// Tests that the manual fallback view icon is not highlighted after presenting
// the add credit card view.
- (void)testCreditCardsButtonStateAfterPresentingAddCreditCard {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Scroll to the right.
  [[EarlGrey selectElementWithMatcher:ManualFallbackFormSuggestionViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeRight)];

  // Tap on the credit card icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the status of the icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_not(grey_userInteractionEnabled())];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Add Credit Cards..." action.
  [[EarlGrey selectElementWithMatcher:ManualFallbackAddCreditCardsMatcher()]
      performAction:grey_tap()];

  // Tap Cancel Button.
  [[EarlGrey selectElementWithMatcher:NavigationBarCancelButton()]
      performAction:grey_tap()];

  // Verify the status of the icons.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      assertWithMatcher:grey_userInteractionEnabled()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      assertWithMatcher:grey_not(grey_sufficientlyVisible())];

  // Verify the keyboard is not cover by the cards view.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that the credit card View Controller is dismissed when tapping the
// keyboard icon.
- (void)testKeyboardIconDismissCreditCardController {
  if ([ChromeEarlGrey isIPadIdiom]) {
    // The keyboard icon is never present in iPads.
    return;
  }
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit cards icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit card controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  // Tap on the keyboard icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit card controller table view and the credit card icon is
  // NOT visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_notVisible()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that the credit card View Controller is dismissed when tapping the
// outside the popover on iPad.
- (void)testIPadTappingOutsidePopOverDismissCreditCardController {
  if (![ChromeEarlGrey isIPadIdiom]) {
    return;
  }
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit cards icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit card controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  // Tap on a point outside of the popover.
  // The way EarlGrey taps doesn't go through the window hierarchy. Because of
  // this, the tap needs to be done in the same window as the popover.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewWindowMatcher()]
      performAction:grey_tapAtPoint(CGPointMake(0, 0))];

  // Verify the credit card controller table view and the credit card icon is
  // NOT visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_notVisible()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that the credit card View Controller is dismissed when tapping the
// keyboard.
- (void)testTappingKeyboardDismissCreditCardControllerPopOver {
  if (![ChromeEarlGrey isIPadIdiom]) {
    return;
  }
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit cards icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit card controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  [[EarlGrey
      selectElementWithMatcher:[KeyboardAppInterface keyboardWindowMatcher]]
      performAction:grey_typeText(@"text")];

  // Verify the credit card controller table view and the credit card icon is
  // NOT visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_notVisible()];
  [[EarlGrey selectElementWithMatcher:ManualFallbackKeyboardIconMatcher()]
      assertWithMatcher:grey_notVisible()];
}

// Tests that after switching fields the content size of the table view didn't
// grow.
- (void)testCreditCardControllerKeepsRightSize {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit cards icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Tap the second element.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementOtherStuff)];

  // Try to scroll.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];
}

// Tests that the credit card View Controller stays on rotation.
- (void)testCreditCardControllerSupportsRotation {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Tap on the credit cards icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the credit card controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  [ChromeEarlGrey rotateDeviceToOrientation:UIDeviceOrientationLandscapeLeft
                                      error:nil];

  // Verify the credit card controller table view is still visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];
}

// Tests that credit card number (for local card) is injected.
// TODO(crbug.com/845472): maybe figure a way to test successfull injection
// when page is https, but right now if we use the https embedded server,
// there's a warning page that stops the flow because of a
// NET::ERR_CERT_AUTHORITY_INVALID.
- (void)testCreditCardLocalNumberDoesntInjectOnHttp {
  [self verifyCreditCardButtonWithTitle:kLocalNumberObfuscated
                        doesInjectValue:@""];
}

// Tests an alert is shown warning the user when trying to fill a credit card
// number in an HTTP form.
- (void)testCreditCardLocalNumberShowsWarningOnHttp {
  [self verifyCreditCardButtonWithTitle:kLocalNumberObfuscated
                        doesInjectValue:@""];
  // Look for the alert.
  [[EarlGrey selectElementWithMatcher:NotSecureWebsiteAlert()]
      assertWithMatcher:grey_not(grey_nil())];
}

// Tests that credit card cardholder is injected.
- (void)testCreditCardCardHolderInjectsCorrectly {
  [self verifyCreditCardButtonWithTitle:kLocalCardHolder
                        doesInjectValue:kLocalCardHolder];
}

// Tests that credit card expiration month is injected.
- (void)testCreditCardExpirationMonthInjectsCorrectly {
  [self verifyCreditCardButtonWithTitle:kLocalCardExpirationMonth
                        doesInjectValue:kLocalCardExpirationMonth];
}

// Tests that credit card expiration year is injected.
- (void)testCreditCardExpirationYearInjectsCorrectly {
  [self verifyCreditCardButtonWithTitle:kLocalCardExpirationYear
                        doesInjectValue:kLocalCardExpirationYear];
}

// Tests that masked credit card offer CVC input.
// TODOD(crbug.com/909748) can't test this one until https tests are possible.
- (void)DISABLED_testCreditCardServerNumberRequiresCVC {
  [AutofillAppInterface saveMaskedCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Wait for the accessory icon to appear.
  GREYAssert(WaitForKeyboardToAppear(), @"Keyboard didn't appear.");

  // Tap on the passwords icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the password controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  // Select a the masked number.
  [[EarlGrey selectElementWithMatcher:grey_buttonTitle(kServerNumberObfuscated)]
      performAction:grey_tap()];

  // Verify the CVC requester is visible.
  [[EarlGrey selectElementWithMatcher:grey_text(@"Confirm Card")]
      assertWithMatcher:grey_notNil()];

  // TODO(crbug.com/845472): maybe figure a way to enter CVC and get the
  // unlocked card result.
}

#pragma mark - Private

- (void)verifyCreditCardButtonWithTitle:(NSString*)title
                        doesInjectValue:(NSString*)result {
  [AutofillAppInterface saveLocalCreditCard];

  // Bring up the keyboard.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::WebViewMatcher()]
      performAction:TapWebElementWithId(kFormElementUsername)];

  // Wait for the accessory icon to appear.
  GREYAssert(WaitForKeyboardToAppear(), @"Keyboard didn't appear.");

  // Tap on the passwords icon.
  [[EarlGrey selectElementWithMatcher:ManualFallbackCreditCardIconMatcher()]
      performAction:grey_tap()];

  // Verify the password controller table view is visible.
  [[EarlGrey
      selectElementWithMatcher:ManualFallbackCreditCardTableViewMatcher()]
      assertWithMatcher:grey_sufficientlyVisible()];

  // Select a field.
  [[EarlGrey selectElementWithMatcher:grey_buttonTitle(title)]
      performAction:grey_tap()];

  // Verify Web Content.
  NSString* javaScriptCondition = [NSString
      stringWithFormat:@"window.document.getElementById('%s').value === '%@'",
                       kFormElementUsername, result];
  XCTAssertTrue(WaitForJavaScriptCondition(javaScriptCondition));
}

@end
