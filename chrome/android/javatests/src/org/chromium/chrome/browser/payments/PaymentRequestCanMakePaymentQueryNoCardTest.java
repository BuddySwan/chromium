// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments;

import android.support.test.filters.MediumTest;

import org.junit.ClassRule;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.autofill.AutofillTestHelper;
import org.chromium.chrome.browser.autofill.PersonalDataManager.CreditCard;
import org.chromium.chrome.browser.payments.PaymentRequestTestRule.AppPresence;
import org.chromium.chrome.browser.payments.PaymentRequestTestRule.FactorySpeed;
import org.chromium.chrome.browser.payments.PaymentRequestTestRule.MainActivityStartCallback;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.ui.test.util.DisableAnimationsTestRule;

import java.util.concurrent.TimeoutException;

/**
 * A payment integration test for checking whether user can make a payment via either payment
 * app or a credit card. This user does not have a complete credit card on file.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class PaymentRequestCanMakePaymentQueryNoCardTest implements MainActivityStartCallback {
    // Disable animations to reduce flakiness.
    @ClassRule
    public static DisableAnimationsTestRule sNoAnimationsRule = new DisableAnimationsTestRule();

    @Rule
    public PaymentRequestTestRule mPaymentRequestTestRule =
            new PaymentRequestTestRule("payment_request_can_make_payment_query_test.html", this);

    @Override
    public void onMainActivityStarted() throws TimeoutException {
        // The user has an incomplete credit card on file. This is not sufficient for
        // canMakePayment() to return true.
        new AutofillTestHelper().setCreditCard(new CreditCard("", "https://example.com", true, true,
                "" /* nameOnCard */, "4111111111111111", "1111", "12", "2050", "visa",
                R.drawable.visa_card, "" /* billingAddressId */, "" /* serverId */));
    }

    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testNoBobPayFactory() throws TimeoutException {
        mPaymentRequestTestRule.openPageAndClickBuyAndWait(
                mPaymentRequestTestRule.getCanMakePaymentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"true"});

        mPaymentRequestTestRule.clickNodeAndWait("hasEnrolledInstrument",
                mPaymentRequestTestRule.getHasEnrolledInstrumentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"false"});
    }

    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testNoAppInFastBobPayInFactory() throws TimeoutException {
        mPaymentRequestTestRule.addPaymentAppFactory(
                AppPresence.NO_APPS, FactorySpeed.FAST_FACTORY);
        mPaymentRequestTestRule.openPageAndClickBuyAndWait(
                mPaymentRequestTestRule.getCanMakePaymentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"true"});

        mPaymentRequestTestRule.clickNodeAndWait("hasEnrolledInstrument",
                mPaymentRequestTestRule.getHasEnrolledInstrumentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"false"});
    }

    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testNoAppsInSlowBobPayFactory() throws TimeoutException {
        mPaymentRequestTestRule.addPaymentAppFactory(
                AppPresence.NO_APPS, FactorySpeed.SLOW_FACTORY);
        mPaymentRequestTestRule.openPageAndClickBuyAndWait(
                mPaymentRequestTestRule.getCanMakePaymentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"true"});

        mPaymentRequestTestRule.clickNodeAndWait("hasEnrolledInstrument",
                mPaymentRequestTestRule.getHasEnrolledInstrumentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"false"});
    }

    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testPayWithFastBobPayFactory() throws TimeoutException {
        mPaymentRequestTestRule.addPaymentAppFactory(
                AppPresence.HAVE_APPS, FactorySpeed.FAST_FACTORY);
        mPaymentRequestTestRule.openPageAndClickBuyAndWait(
                mPaymentRequestTestRule.getCanMakePaymentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"true"});
    }

    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testPayWithSlowBobPayFactory() throws TimeoutException {
        mPaymentRequestTestRule.addPaymentAppFactory(
                AppPresence.HAVE_APPS, FactorySpeed.SLOW_FACTORY);
        mPaymentRequestTestRule.openPageAndClickBuyAndWait(
                mPaymentRequestTestRule.getCanMakePaymentQueryResponded());
        mPaymentRequestTestRule.expectResultContains(new String[] {"true"});
    }
}
