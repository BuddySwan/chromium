// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.Destroyable;
import org.chromium.chrome.browser.ui.TabObscuringHandler;

/**
 * Handles the visibility update of the activity tab.
 */
public class AccessibilityVisibilityHandler implements Destroyable {
    private final ActivityTabProvider.ActivityTabTabObserver mActivityTabObserver;
    private TabImpl mTab;

    public AccessibilityVisibilityHandler(ActivityLifecycleDispatcher lifecycleDispatcher,
            ActivityTabProvider activityTabProvider, TabObscuringHandler tabObscuringHandler) {
        mActivityTabObserver = new ActivityTabProvider.ActivityTabTabObserver(activityTabProvider) {
            @Override
            public void onObservingDifferentTab(Tab tab) {
                if (mTab == tab) return;

                TabImpl tabImpl = (TabImpl) tab;
                if (mTab != null) tabObscuringHandler.removeObserver(mTab);
                if (tabImpl != null) tabObscuringHandler.addObserver(tabImpl);
                mTab = tabImpl;
            }

            @Override
            public void onContentChanged(Tab tab) {
                mTab.updateObscured(tabObscuringHandler.isViewObscuringAllTabs());
            }
        };
        lifecycleDispatcher.register(this);
    }

    // Destroyable

    @Override
    public void destroy() {
        mActivityTabObserver.destroy();
        mTab = null;
    }
}
