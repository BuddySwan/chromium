// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.webapps;

import static org.junit.Assert.assertTrue;

import android.support.test.filters.LargeTest;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.RuleChain;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.dependency_injection.ChromeActivityCommonsModule;
import org.chromium.chrome.browser.dependency_injection.ModuleOverridesRule;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.LifecycleObserver;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.browser.webapps.WebApkInfoBuilder;
import org.chromium.net.test.EmbeddedTestServer;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeoutException;

/**
 * Tests that the expected classes are constructed when a {@link WebApkActivity} is launched.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class WebApkInitializationTest {
    /**
     * {@link ActivityLifecycleDispatcher} wrapper which tracks {@link LifecycleObserver}
     * registrations.
     */
    private static class TrackingActivityLifecycleDispatcher
            implements ActivityLifecycleDispatcher {
        private ActivityLifecycleDispatcher mRealActivityLifecycleDispatcher;
        private Set<String> mRegisteredObserverClassNames = new HashSet<>();

        public void init(ActivityLifecycleDispatcher realActivityLifecycleDispatcher) {
            mRealActivityLifecycleDispatcher = realActivityLifecycleDispatcher;
        }

        /**
         * Returns set of all the {@link LifecycleObserver} subclasses which have registered with
         * the {@link ActivityLifecycleDispatcher}.
         */
        public Set<String> getRegisteredObserverClassNames() {
            return mRegisteredObserverClassNames;
        }

        @Override
        public void register(LifecycleObserver observer) {
            mRegisteredObserverClassNames.add(observer.getClass().getName());
            mRealActivityLifecycleDispatcher.register(observer);
        }

        @Override
        public void unregister(LifecycleObserver observer) {
            mRealActivityLifecycleDispatcher.unregister(observer);
        }

        @Override
        public @ActivityState int getCurrentActivityState() {
            return mRealActivityLifecycleDispatcher.getCurrentActivityState();
        }

        @Override
        public boolean isNativeInitializationFinished() {
            return true;
        }
    }

    private final TrackingActivityLifecycleDispatcher mTrackingActivityLifecycleDispatcher =
            new TrackingActivityLifecycleDispatcher();

    private final TestRule mModuleOverridesRule = new ModuleOverridesRule().setOverride(
            ChromeActivityCommonsModule.Factory.class, (activity, lifecycleDispatcher) -> {
                mTrackingActivityLifecycleDispatcher.init(lifecycleDispatcher);
                return new ChromeActivityCommonsModule(
                        activity, mTrackingActivityLifecycleDispatcher);
            });

    private final WebApkActivityTestRule mActivityRule = new WebApkActivityTestRule();

    @Rule
    public final TestRule mRuleChain =
            RuleChain.outerRule(mModuleOverridesRule).around(mActivityRule);

    /**
     * Test that {@link WebappActionsNotificationManager},
     * {@link WebappDisclosureSnackbarController} and {@link WebApkActivityLifecycleUmaTracker} are
     * constructed when a {@link WebApkActivity} is launched.
     */
    @Test
    @LargeTest
    @Feature({"WebApk"})
    public void testInitialization() throws TimeoutException {
        EmbeddedTestServer embeddedTestServer =
                mActivityRule.getEmbeddedTestServerRule().getServer();
        WebApkInfoBuilder webApkInfoBuilder = new WebApkInfoBuilder(
                "org.chromium.webapk.for.testing",
                embeddedTestServer.getURL("/chrome/test/data/banners/manifest_test_page.html"));
        mActivityRule.startWebApkActivity(webApkInfoBuilder.build());

        Set<String> registeredObserverClassNames =
                mTrackingActivityLifecycleDispatcher.getRegisteredObserverClassNames();
        assertTrue(registeredObserverClassNames.contains(
                WebappActionsNotificationManager.class.getName()));
        assertTrue(registeredObserverClassNames.contains(
                WebappDisclosureSnackbarController.class.getName()));
        assertTrue(registeredObserverClassNames.contains(
                WebApkActivityLifecycleUmaTracker.class.getName()));
    }
}
