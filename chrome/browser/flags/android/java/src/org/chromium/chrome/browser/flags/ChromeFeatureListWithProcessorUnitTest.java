// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.flags;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.Features.DisableFeatures;
import org.chromium.chrome.test.util.browser.Features.EnableFeatures;

import java.util.Collections;

/**
 * Shows the behavior of {@link ChromeFeatureList} in Robolectric unit tests when the rule
 * Features.JUnitProcessor is present.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class ChromeFeatureListWithProcessorUnitTest {
    @Rule
    public TestRule mProcessor = new Features.JUnitProcessor();

    /**
     * In unit tests, all flags checked must have their value specified.
     */
    @Test(expected = IllegalArgumentException.class)
    public void testNoOverridesDefaultDisabled_throws() {
        ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_DISABLED);
    }

    /**
     * In unit tests, all flags checked must have their value specified.
     */
    @Test(expected = IllegalArgumentException.class)
    public void testNoOverridesDefaultEnabled_throws() {
        ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_ENABLED);
    }

    /**
     * In unit tests, flags may have their value specified by the EnableFeatures annotation.
     */
    @Test
    @EnableFeatures(ChromeFeatureList.TEST_DEFAULT_DISABLED)
    public void testAnnotationEnabled_returnsEnabled() {
        assertTrue(ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_DISABLED));
    }

    /**
     * In unit tests, flags may have their value specified by the DisableFeatures annotation.
     */
    @Test
    @DisableFeatures(ChromeFeatureList.TEST_DEFAULT_DISABLED)
    public void testAnnotationDisabled_returnsDisabled() {
        assertFalse(ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_DISABLED));
    }

    /**
     * In unit tests, flags may have their value specified by calling
     * {@link ChromeFeatureList#setTestFeatures(java.util.Map)}.
     */
    @Test
    public void testSetTestFeaturesEnabled_returnsEnabled() {
        ChromeFeatureList.setTestFeatures(
                Collections.singletonMap(ChromeFeatureList.TEST_DEFAULT_DISABLED, true));
        assertTrue(ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_DISABLED));
        ChromeFeatureList.setTestFeatures(null);
    }

    /**
     * In unit tests, flags may have their value specified by calling
     * {@link ChromeFeatureList#setTestFeatures(java.util.Map)}.
     */
    @Test
    public void testSetTestFeaturesDisabled_returnsDisabled() {
        ChromeFeatureList.setTestFeatures(
                Collections.singletonMap(ChromeFeatureList.TEST_DEFAULT_DISABLED, false));
        assertFalse(ChromeFeatureList.isEnabled(ChromeFeatureList.TEST_DEFAULT_DISABLED));
        ChromeFeatureList.setTestFeatures(null);
    }
}
