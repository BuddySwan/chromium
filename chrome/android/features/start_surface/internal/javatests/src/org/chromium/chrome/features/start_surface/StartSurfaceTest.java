// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.features.start_surface;

import static android.support.test.espresso.Espresso.onView;
import static android.support.test.espresso.Espresso.pressBack;
import static android.support.test.espresso.action.ViewActions.click;
import static android.support.test.espresso.action.ViewActions.pressKey;
import static android.support.test.espresso.action.ViewActions.typeText;
import static android.support.test.espresso.assertion.ViewAssertions.matches;
import static android.support.test.espresso.matcher.ViewMatchers.Visibility.GONE;
import static android.support.test.espresso.matcher.ViewMatchers.isDisplayed;
import static android.support.test.espresso.matcher.ViewMatchers.isRoot;
import static android.support.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static android.support.test.espresso.matcher.ViewMatchers.withId;
import static android.support.test.espresso.matcher.ViewMatchers.withParent;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import static org.chromium.chrome.test.util.ViewUtils.waitForView;

import android.support.test.InstrumentationRegistry;
import android.support.test.espresso.contrib.RecyclerViewActions;
import android.support.test.filters.MediumTest;
import android.view.KeyEvent;
import android.view.ViewGroup;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.compositor.layouts.OverviewModeState;
import org.chromium.chrome.browser.flags.CachedFeatureFlags;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.tasks.tab_management.TabUiTestHelper;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.OverviewModeBehaviorWatcher;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.net.test.EmbeddedTestServer;
import org.chromium.ui.test.util.UiRestriction;

import java.util.concurrent.ExecutionException;

/** Integration tests of the {@link StartSurface}. */
@RunWith(ChromeJUnit4ClassRunner.class)
@Restriction(UiRestriction.RESTRICTION_TYPE_PHONE)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        "enable-features=" + ChromeFeatureList.START_SURFACE_ANDROID + "<Study",
        "force-fieldtrials=Study/Group"})
public class StartSurfaceTest {
    private static final String BASE_PARAMS =
            "force-fieldtrial-params=Study.Group:start_surface_variation";

    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @Rule
    public TestRule mProcessor = new Features.InstrumentationProcessor();

    private String mUrl;

    @Before
    public void setUp() {
        CachedFeatureFlags.setStartSurfaceEnabledForTesting(true);

        EmbeddedTestServer testServer =
                EmbeddedTestServer.createAndStartServer(InstrumentationRegistry.getContext());
        mActivityTestRule.startMainActivityFromLauncher();

        mUrl = testServer.getURL("/chrome/test/data/android/navigate/simple.html");
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/tasksonly"})
    public void testShowAndHideTasksOnlySurface() {
        TabUiTestHelper.enterTabSwitcher(mActivityTestRule.getActivity());

        onView(withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.mv_tiles_container))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.tasks_surface_body))
                .check(matches(isDisplayed()));

        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().hideOverview(false));
        assertFalse(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/omniboxonly"})
    public void testShowAndHideOmniboxOnlySurface() {
        final ChromeTabbedActivity cta = mActivityTestRule.getActivity();
        TabUiTestHelper.enterTabSwitcher(cta);

        onView(withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.mv_tiles_container))
                .check(matches(withEffectiveVisibility(GONE)));
        onView(withId(org.chromium.chrome.tab_ui.R.id.tasks_surface_body))
                .check(matches(isDisplayed()));

        onView(withId(org.chromium.chrome.tab_ui.R.id.incognito_switch))
                .check(matches(withEffectiveVisibility(GONE)));

        TabUiTestHelper.createTabs(cta, true, 1);
        TabUiTestHelper.verifyTabModelTabCount(cta, 1, 1);
        TabUiTestHelper.enterTabSwitcher(cta);
        onView(withId(org.chromium.chrome.tab_ui.R.id.incognito_switch))
                .check(matches(isDisplayed()));

        TestThreadUtils.runOnUiThreadBlocking(
                () -> cta.getTabModelSelector().getModel(true).closeAllTabs());
        TabUiTestHelper.verifyTabModelTabCount(cta, 1, 0);
        assertTrue(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());
        onView(withId(org.chromium.chrome.tab_ui.R.id.incognito_switch))
                .check(matches(withEffectiveVisibility(GONE)));

        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().hideOverview(false));
        assertFalse(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/single"})
    public void testShowAndHideHomePageInSingleSurface() {
        // TODO(crbug.com/1025296): Set cached flag before starting the activity and mimic clicking
        // the 'home' button to show the single start surface home page.
        TestThreadUtils.runOnUiThreadBlocking(
                ()
                        -> mActivityTestRule.getActivity()
                                   .getStartSurface()
                                   .getController()
                                   .setOverviewState(OverviewModeState.SHOWING_HOMEPAGE));
        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().showOverview(false));
        assertTrue(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());

        onView(withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.start_surface.R.id.search_box_text))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.mv_tiles_container))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.tab_switcher_title))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.carousel_tab_switcher_container))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.tasks_surface_body))
                .check(matches(isDisplayed()));

        // Note that onView(R.id.more_tabs).perform(click()) can not be used since it requires 90
        // percent of the view's area is displayed to the users. However, this view has negative
        // margin which makes the percentage is less than 90.
        // TODO(crbug.com/1025296): Investigate whether this would be a problem for real users.
        try {
            TestThreadUtils.runOnUiThreadBlocking(
                    ()
                            -> mActivityTestRule.getActivity()
                                       .findViewById(org.chromium.chrome.tab_ui.R.id.more_tabs)
                                       .performClick());
        } catch (ExecutionException e) {
            assertTrue(false);
        }
        onView(isRoot()).check((r, e) -> {
            waitForView((ViewGroup) r,
                    withId(org.chromium.chrome.start_surface.R.id.secondary_tasks_surface_view));
        });

        pressBack();
        onView(isRoot()).check((r, e) -> {
            waitForView((ViewGroup) r,
                    withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view));
        });

        OverviewModeBehaviorWatcher hideWatcher =
                TabUiTestHelper.createOverviewHideWatcher(mActivityTestRule.getActivity());
        onView(allOf(withParent(withId(
                             org.chromium.chrome.tab_ui.R.id.carousel_tab_switcher_container)),
                       withId(org.chromium.chrome.tab_ui.R.id.tab_list_view)))
                .perform(RecyclerViewActions.actionOnItemAtPosition(0, click()));
        hideWatcher.waitForBehavior();
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/single"})
    public void testShowAndHideTabSwitcherInSingleSurface() {
        TabUiTestHelper.enterTabSwitcher(mActivityTestRule.getActivity());

        onView(withId(org.chromium.chrome.start_surface.R.id.secondary_tasks_surface_view))
                .check(matches(isDisplayed()));

        OverviewModeBehaviorWatcher hideWatcher =
                TabUiTestHelper.createOverviewHideWatcher(mActivityTestRule.getActivity());
        onView(allOf(withParent(withId(org.chromium.chrome.tab_ui.R.id.tasks_surface_body)),
                       withId(org.chromium.chrome.tab_ui.R.id.tab_list_view)))
                .perform(RecyclerViewActions.actionOnItemAtPosition(0, click()));
        hideWatcher.waitForBehavior();
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/single"})
    @DisabledTest(message = "crbug.com/1051643")
    public void testSearchInSingleSurface() {
        // TODO(crbug.com/1025296): Set cached flag before starting the activity and mimic clicking
        // the 'home' button to show the single start surface home page.
        TestThreadUtils.runOnUiThreadBlocking(
                ()
                        -> mActivityTestRule.getActivity()
                                   .getStartSurface()
                                   .getController()
                                   .setOverviewState(OverviewModeState.SHOWING_HOMEPAGE));
        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().showOverview(false));
        assertTrue(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(1));

        OverviewModeBehaviorWatcher hideWatcher =
                TabUiTestHelper.createOverviewHideWatcher(mActivityTestRule.getActivity());
        onView(withId(org.chromium.chrome.start_surface.R.id.search_box_text))
                .perform(typeText(mUrl));
        onView(withId(org.chromium.chrome.start_surface.R.id.url_bar))
                .perform(pressKey(KeyEvent.KEYCODE_ENTER));
        hideWatcher.waitForBehavior();
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(2));

        // Search in incognito mode.
        TestThreadUtils.runOnUiThreadBlocking(
                ()
                        -> mActivityTestRule.getActivity()
                                   .getStartSurface()
                                   .getController()
                                   .setOverviewState(OverviewModeState.SHOWING_HOMEPAGE));
        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().showOverview(false));
        onView(withId(org.chromium.chrome.start_surface.R.id.incognito_switch)).perform(click());
        assertTrue(mActivityTestRule.getActivity().getTabModelSelector().isIncognitoSelected());

        hideWatcher = TabUiTestHelper.createOverviewHideWatcher(mActivityTestRule.getActivity());
        onView(allOf(withId(org.chromium.chrome.start_surface.R.id.search_box_text), isDisplayed()))
                .perform(typeText(mUrl));
        onView(withId(org.chromium.chrome.start_surface.R.id.url_bar))
                .perform(pressKey(KeyEvent.KEYCODE_ENTER));
        hideWatcher.waitForBehavior();
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(1));
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/single"})
    public void testTapMVTilesInSingleSurface() {
        // TODO(crbug.com/1025296): Set cached flag before starting the activity and mimic clicking
        // the 'home' button to show the single start surface home page.
        TestThreadUtils.runOnUiThreadBlocking(
                ()
                        -> mActivityTestRule.getActivity()
                                   .getStartSurface()
                                   .getController()
                                   .setOverviewState(OverviewModeState.SHOWING_HOMEPAGE));
        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().showOverview(false));
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(1));

        ViewGroup mvTilesContainer = (ViewGroup) mActivityTestRule.getActivity().findViewById(
                org.chromium.chrome.tab_ui.R.id.mv_tiles_layout);
        assertTrue(mvTilesContainer.getChildCount() > 0);

        OverviewModeBehaviorWatcher hideWatcher =
                TabUiTestHelper.createOverviewHideWatcher(mActivityTestRule.getActivity());
        try {
            // TODO (crbug.com/1025296): Find a way to perform click on a child at index 0 in
            // LinearLayout with Espresso. Note that we do not have 'withParentIndex' so far.
            TestThreadUtils.runOnUiThreadBlocking(
                    () -> mvTilesContainer.getChildAt(0).performClick());
        } catch (ExecutionException e) {
            assertTrue(false);
        }
        hideWatcher.waitForBehavior();
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(2));

        // Press back button should close the tab opened from the Start surface.
        OverviewModeBehaviorWatcher showWatcher =
                TabUiTestHelper.createOverviewShowWatcher(mActivityTestRule.getActivity());
        pressBack();
        showWatcher.waitForBehavior();
        assertThat(
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel().getCount(),
                equalTo(1));
    }

    @Test
    @MediumTest
    @Feature({"StartSurface"})
    @CommandLineFlags.Add({BASE_PARAMS + "/twopanes"})
    public void testShowAndHideTwoPanesSurface() {
        TestThreadUtils.runOnUiThreadBlocking(
                ()
                        -> mActivityTestRule.getActivity()
                                   .getStartSurface()
                                   .getController()
                                   .setOverviewState(OverviewModeState.SHOWING_TABSWITCHER));
        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().showOverview(false));
        assertTrue(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());

        onView(withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.start_surface.R.id.ss_bottom_bar))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.mv_tiles_container))
                .check(matches(isDisplayed()));
        onView(withId(org.chromium.chrome.tab_ui.R.id.tasks_surface_body))
                .check(matches(isDisplayed()));

        onView(withId(org.chromium.chrome.start_surface.R.id.ss_explore_tab)).perform(click());
        onView(isRoot()).check((r, e) -> {
            waitForView((ViewGroup) r,
                    withId(org.chromium.chrome.start_surface.R.id.start_surface_explore_view));
        });

        pressBack();
        onView(isRoot()).check((r, e) -> {
            waitForView((ViewGroup) r,
                    withId(org.chromium.chrome.start_surface.R.id.primary_tasks_surface_view));
        });

        TestThreadUtils.runOnUiThreadBlocking(
                () -> mActivityTestRule.getActivity().getLayoutManager().hideOverview(false));
        assertFalse(mActivityTestRule.getActivity().getLayoutManager().overviewVisible());
    }
}

// TODO(crbug.com/1033909): Add more integration tests.
