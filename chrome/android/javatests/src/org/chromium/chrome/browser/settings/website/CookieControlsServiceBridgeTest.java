// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.settings.website;

import android.support.test.InstrumentationRegistry;
import android.support.test.filters.SmallTest;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CallbackHelper;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.browser.Features.EnableFeatures;
import org.chromium.components.content_settings.CookieControlsMode;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.net.test.EmbeddedTestServer;

/**
 * Integration tests for CookieControlsServiceBridge.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@EnableFeatures(ChromeFeatureList.IMPROVED_COOKIE_CONTROLS)
public class CookieControlsServiceBridgeTest {
    private class TestCallbackHandler
            implements CookieControlsServiceBridge.CookieControlsServiceObserver {
        private CallbackHelper mHelper;

        public TestCallbackHandler(CallbackHelper helper) {
            mHelper = helper;
        }

        @Override
        public void sendCookieControlsUIChanges(boolean checked, boolean enforced) {
            mChecked = checked;
            mEnforced = enforced;
            mHelper.notifyCalled();
        }
    }

    @Rule
    public ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);
    private EmbeddedTestServer mTestServer;
    private CallbackHelper mCallbackHelper;
    private TestCallbackHandler mCallbackHandler;
    private CookieControlsServiceBridge mCookieControlsServiceBridge;
    private boolean mChecked;
    private boolean mEnforced;

    @Before
    public void setUp() throws Exception {
        mCallbackHelper = new CallbackHelper();
        mCallbackHandler = new TestCallbackHandler(mCallbackHelper);
        mActivityTestRule.startMainActivityOnBlankPage();
        mTestServer = EmbeddedTestServer.createAndStartServer(InstrumentationRegistry.getContext());
    }

    @After
    public void tearDown() {
        mTestServer.stopAndDestroyServer();
    }

    private void setThirdPartyCookieBlocking(boolean enabled) {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            PrefServiceBridge.getInstance().setBoolean(Pref.BLOCK_THIRD_PARTY_COOKIES, enabled);
        });
    }

    private void setCookieControlsMode(@CookieControlsMode int mode) {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            PrefServiceBridge.getInstance().setInteger(Pref.COOKIE_CONTROLS_MODE, mode);
        });
    }

    /**
     * Test changing the bridge triggers callback for correct toggle state.
     */
    @Test
    @SmallTest
    public void testCookieSettingsCheckedChanges() throws Exception {
        setThirdPartyCookieBlocking(false);
        setCookieControlsMode(CookieControlsMode.OFF);
        final String url = mTestServer.getURL("/chrome/test/data/android/cookie.html");
        Tab tab = mActivityTestRule.loadUrlInNewTab(url, true); // incognito tab

        // Test that the toggle switches on.
        boolean expectedChecked = true;
        mChecked = false;
        int currentCallCount = mCallbackHelper.getCallCount();
        // Create cookie settings bridge and wait for desired callbacks.
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mCookieControlsServiceBridge = new CookieControlsServiceBridge(
                    mCallbackHandler, Profile.fromWebContents(tab.getWebContents()));
        });
        setCookieControlsMode(CookieControlsMode.INCOGNITO_ONLY);
        mCallbackHelper.waitForCallback(currentCallCount, 1);
        Assert.assertEquals(expectedChecked, mChecked);

        // Test that the toggle switches off.
        expectedChecked = false;
        mChecked = true;
        currentCallCount = mCallbackHelper.getCallCount();
        setCookieControlsMode(CookieControlsMode.OFF);
        mCallbackHelper.waitForCallback(currentCallCount, 1);
        Assert.assertEquals(expectedChecked, mChecked);

        // Test that the toggle switches back on and enforced (by settings)
        expectedChecked = true;
        mChecked = false;
        boolean expectedEnforced = true;
        mEnforced = false;
        currentCallCount = mCallbackHelper.getCallCount();
        setThirdPartyCookieBlocking(true);
        mCallbackHelper.waitForCallback(currentCallCount, 1);
        Assert.assertEquals(expectedChecked, mChecked);
        Assert.assertEquals(expectedEnforced, mEnforced);
    }

    /**
     * Test the ability to set the cookie controls mode pref through the bridge.
     */
    @Test
    @SmallTest
    public void testCookieBridgeWithTPCookiesDisabled() throws Exception {
        setThirdPartyCookieBlocking(false);
        setCookieControlsMode(CookieControlsMode.OFF);
        final String url = mTestServer.getURL("/chrome/test/data/android/cookie.html");
        Tab tab = mActivityTestRule.loadUrlInNewTab(url, true); // incognito tab.

        boolean expectedChecked = true;
        mChecked = false;
        int currentCallCount = mCallbackHelper.getCallCount();
        // Create cookie controls service bridge and wait for desired callbacks.
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mCookieControlsServiceBridge = new CookieControlsServiceBridge(
                    mCallbackHandler, Profile.fromWebContents(tab.getWebContents()));

            mCookieControlsServiceBridge.handleCookieControlsToggleChanged(true);

            Assert.assertEquals("CookieControlsMode should be incognito_only",
                    PrefServiceBridge.getInstance().getInteger(Pref.COOKIE_CONTROLS_MODE),
                    CookieControlsMode.INCOGNITO_ONLY);
        });
        mCallbackHelper.waitForCallback(currentCallCount, 1);
        Assert.assertEquals(expectedChecked, mChecked);
    }
}
