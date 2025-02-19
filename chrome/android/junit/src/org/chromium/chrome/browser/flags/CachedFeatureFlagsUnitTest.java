// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.flags;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.BaseSwitches;
import org.chromium.base.CommandLine;
import org.chromium.base.SysUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.util.AccessibilityUtil;
import org.chromium.chrome.test.util.browser.Features;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Unit Tests for {@link CachedFeatureFlags}.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class CachedFeatureFlagsUnitTest {
    @Rule
    public TestRule mProcessor = new Features.JUnitProcessor();

    @Mock
    CommandLine mCommandLine;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);

        when(mCommandLine.isNativeImplementation()).thenReturn(true);
        CommandLine.setInstanceForTesting(mCommandLine);

        AccessibilityUtil.setAccessibilityEnabledForTesting(false);
        CachedFeatureFlags.resetFlagsForTesting();
    }

    @After
    public void tearDown() {
        CommandLine.reset();
        CachedFeatureFlags.resetFlagsForTesting();
        ChromeFeatureList.setTestFeatures(null);
        AccessibilityUtil.setAccessibilityEnabledForTesting(null);
        SysUtils.resetForTesting();
    }

    public static final String FEATURE_A = "FeatureA";
    public static final String FEATURE_B = "FeatureB";

    @Test(expected = IllegalArgumentException.class)
    public void testNativeInitializedNoDefault_throwsException() {
        // Setup FeatureA in ChromeFeatureList but not in the defaults.
        Map<String, Boolean> testFeatures = Collections.singletonMap(FEATURE_A, false);
        ChromeFeatureList.setTestFeatures(testFeatures);

        // Assert {@link CachedFeatureFlags} throws an exception.
        CachedFeatureFlags.cacheNativeFlags(Collections.singletonList(FEATURE_A));
        assertFalse(CachedFeatureFlags.isEnabled(FEATURE_A));
    }

    private static final Map<String, Boolean> A_OFF_B_ON = new HashMap<String, Boolean>() {
        {
            put(FEATURE_A, false);
            put(FEATURE_B, true);
        }
    };
    private static final Map<String, Boolean> A_OFF_B_OFF = new HashMap<String, Boolean>() {
        {
            put(FEATURE_A, false);
            put(FEATURE_B, false);
        }
    };
    private static final Map<String, Boolean> A_ON_B_OFF = new HashMap<String, Boolean>() {
        {
            put(FEATURE_A, true);
            put(FEATURE_B, false);
        }
    };
    private static final Map<String, Boolean> A_ON_B_ON = new HashMap<String, Boolean>() {
        {
            put(FEATURE_A, true);
            put(FEATURE_B, true);
        }
    };
    private static final List<String> FEATURES_A_AND_B = Arrays.asList(FEATURE_A, FEATURE_B);

    private static void assertIsEnabledMatches(Map<String, Boolean> state) {
        assertEquals(state.get(FEATURE_A), CachedFeatureFlags.isEnabled(FEATURE_A));
        assertEquals(state.get(FEATURE_B), CachedFeatureFlags.isEnabled(FEATURE_B));
    }

    @Test
    public void testNativeInitialized_getsFromChromeFeatureList() {
        Map<String, Boolean> previousDefaults =
                CachedFeatureFlags.swapDefaultsForTesting(A_OFF_B_OFF);

        try {
            // Cache native flags, meaning values from ChromeFeatureList should be used from now on.
            ChromeFeatureList.setTestFeatures(A_OFF_B_ON);
            CachedFeatureFlags.cacheNativeFlags(FEATURES_A_AND_B);

            // Assert {@link CachedFeatureFlags} uses the values from {@link ChromeFeatureList}.
            assertIsEnabledMatches(A_OFF_B_ON);
        } finally {
            CachedFeatureFlags.swapDefaultsForTesting(previousDefaults);
        }
    }

    @Test
    public void testNativeNotInitializedNotCached_useDefault() {
        Map<String, Boolean> previousDefaults =
                CachedFeatureFlags.swapDefaultsForTesting(A_OFF_B_OFF);

        try {
            // Do not cache values from native. There are no values stored in prefs either.
            ChromeFeatureList.setTestFeatures(A_OFF_B_ON);

            // Query the flags to make sure the default values are returned.
            assertIsEnabledMatches(A_OFF_B_OFF);

            // Now do cache the values from ChromeFeatureList.
            CachedFeatureFlags.cacheNativeFlags(FEATURES_A_AND_B);

            // Verify that {@link CachedFeatureFlags} returns consistent values in the same run.
            assertIsEnabledMatches(A_OFF_B_OFF);
        } finally {
            CachedFeatureFlags.swapDefaultsForTesting(previousDefaults);
        }
    }

    @Test
    public void testNativeNotInitializedPrefsCached_getsFromPrefs() {
        Map<String, Boolean> previousDefaults =
                CachedFeatureFlags.swapDefaultsForTesting(A_OFF_B_OFF);

        try {
            // Cache native flags, meaning values from ChromeFeatureList should be used from now on.
            ChromeFeatureList.setTestFeatures(A_OFF_B_ON);
            CachedFeatureFlags.cacheNativeFlags(FEATURES_A_AND_B);
            assertIsEnabledMatches(A_OFF_B_ON);

            // Pretend the app was restarted. The SharedPrefs should remain.
            CachedFeatureFlags.resetFlagsForTesting();

            // Simulate ChromeFeatureList retrieving new, different values for the flags.
            ChromeFeatureList.setTestFeatures(A_ON_B_ON);

            // Do not cache new values, but query the flags to make sure the values stored to prefs
            // are returned. Neither the defaults (false/false) or the ChromeFeatureList values
            // (true/true) should be returned.
            assertIsEnabledMatches(A_OFF_B_ON);

            // Now do cache the values from ChromeFeatureList.
            CachedFeatureFlags.cacheNativeFlags(FEATURES_A_AND_B);

            // Verify that {@link CachedFeatureFlags} returns consistent values in the same run.
            assertIsEnabledMatches(A_OFF_B_ON);

            // Pretend the app was restarted again.
            CachedFeatureFlags.resetFlagsForTesting();

            // The SharedPrefs should retain the latest values.
            assertIsEnabledMatches(A_ON_B_ON);
        } finally {
            CachedFeatureFlags.swapDefaultsForTesting(previousDefaults);
        }
    }

    @Test
    public void testSetForTesting_returnsForcedValue() {
        Map<String, Boolean> previousDefaults =
                CachedFeatureFlags.swapDefaultsForTesting(A_OFF_B_OFF);

        try {
            // Do not cache values from native. There are no values stored in prefs either.
            // Query the flags to make sure the default values are returned.
            assertIsEnabledMatches(A_OFF_B_OFF);

            // Force a feature flag.
            CachedFeatureFlags.setForTesting(FEATURE_A, true);

            // Verify that the forced value is returned.
            assertIsEnabledMatches(A_ON_B_OFF);

            // Remove the forcing.
            CachedFeatureFlags.setForTesting(FEATURE_A, null);

            // Verify that the forced value is not returned anymore.
            assertIsEnabledMatches(A_OFF_B_OFF);
        } finally {
            CachedFeatureFlags.swapDefaultsForTesting(previousDefaults);
        }
    }

    @Test
    // clang-format off
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_NoEnabledFlags_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_NoEnabledFlags_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(false);
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_Layout_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_Layout_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();

        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_LayoutGroup_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_LayoutGroup_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_Group_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_Group_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_Continuation_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_Continuation_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_AllFlags_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_AllFlags_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);

        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_LayoutContinuation_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_LayoutContinuation_disabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertFalse(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertFalse(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID})
    public void testCacheGridTabSwitcher_HighEnd_GroupContinuation_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.DISABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }

    @Test
    // clang-format off
    @Features.EnableFeatures({ChromeFeatureList.TAB_GROUPS_ANDROID,
                                ChromeFeatureList.TAB_GROUPS_CONTINUATION_ANDROID})
    @Features.DisableFeatures({ChromeFeatureList.DUET_TABSTRIP_INTEGRATION_ANDROID,
                                ChromeFeatureList.TAB_GRID_LAYOUT_ANDROID})
    public void testCacheGridTabSwitcher_LowEnd_GroupContinuation_enabled() {
        // clang-format on
        when(mCommandLine.hasSwitch(BaseSwitches.ENABLE_LOW_END_DEVICE_MODE)).thenReturn(true);
        CachedFeatureFlags.cacheNativeTabSwitcherUiFlags();

        CachedFeatureFlags.resetFlagsForTesting();
        assertTrue(CachedFeatureFlags.isGridTabSwitcherEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidEnabled());
        assertTrue(CachedFeatureFlags.isTabGroupsAndroidContinuationEnabled());
    }
}
