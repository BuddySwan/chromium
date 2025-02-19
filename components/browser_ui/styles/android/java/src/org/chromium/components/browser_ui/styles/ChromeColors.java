// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.styles;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.support.v7.content.res.AppCompatResources;

import androidx.annotation.ColorRes;

import org.chromium.base.ApiCompatibilityUtils;

/**
 * Provides common default colors for Chrome UI.
 */
public class ChromeColors {
    /**
     * Determines the default theme color used for toolbar based on the provided parameters.
     *
     * @param res {@link Resources} used to retrieve colors.
     * @param forceDarkBgColor When true, returns the default dark-mode color; otherwise returns
     *        adaptive default color.
     * @return The default theme color.
     */
    public static @ColorRes int getDefaultThemeColor(Resources res, boolean forceDarkBgColor) {
        return forceDarkBgColor
                ? ApiCompatibilityUtils.getColor(res, R.color.toolbar_background_primary_dark)
                : ApiCompatibilityUtils.getColor(res, R.color.toolbar_background_primary);
    }

    /**
     * Returns the primary background color used as native page background based on the given
     * parameters.
     *
     * @param res The {@link Resources} used to retrieve colors.
     * @param forceDarkBgColor When true, returns the dark-mode primary background color; otherwise
     *        returns adaptive primary background color.
     * @return The primary background color.
     */
    public static @ColorRes int getPrimaryBackgroundColor(Resources res, boolean forceDarkBgColor) {
        return forceDarkBgColor
                ? ApiCompatibilityUtils.getColor(res, org.chromium.ui.R.color.dark_primary_color)
                : ApiCompatibilityUtils.getColor(res, org.chromium.ui.R.color.modern_primary_color);
    }

    /**
     * Returns the primary icon tint resource to use based on the current parameters and whether
     * the app is in night mode.
     *
     * @param forceLightIconTint When true, returns the light tint color res; otherwise returns
     *         adaptive primary icon tint color res.
     * @return The {@link ColorRes} for the icon tint.
     */
    public static @ColorRes int getPrimaryIconTintRes(boolean forceLightIconTint) {
        return forceLightIconTint ? R.color.default_icon_color_light_tint_list
                                  : R.color.default_icon_color_tint_list;
    }

    /**
     * Returns the primary icon tint to use based on the current parameters and whether the app is
     * in night mode.
     *
     * @param context The {@link Context} used to retrieve colors.
     * @param forceLightIconTint When true, returns the light tint color res; otherwise returns
     *         adaptive primary icon tint color res.
     * @return The {@link ColorStateList} for the icon tint.
     */
    public static ColorStateList getPrimaryIconTint(Context context, boolean forceLightIconTint) {
        return AppCompatResources.getColorStateList(
                context, getPrimaryIconTintRes(forceLightIconTint));
    }

    /**
     * Returns the secondary icon tint resource to use based on the current parameters and whether
     * the app is in night mode.
     *
     * @param forceLightIconTint When true, returns the light tint color res; otherwise returns
     *         adaptive secondary icon tint color res.
     * @return The {@link ColorRes} for the icon tint.
     */
    public static @ColorRes int getSecondaryIconTintRes(boolean forceLightIconTint) {
        return forceLightIconTint ? R.color.default_icon_color_secondary_light_tint_list
                                  : R.color.default_icon_color_secondary_tint_list;
    }

    /**
     * Returns the secondary icon tint to use based on the current parameters and whether the app
     * is in night mode.
     *
     * @param context The {@link Context} used to retrieve colors.
     * @param forceLightIconTint When true, returns the light tint color res; otherwise returns
     *         adaptive secondary icon tint color res.
     * @return The {@link ColorStateList} for the icon tint.
     */
    public static ColorStateList getSecondaryIconTint(Context context, boolean forceLightIconTint) {
        return AppCompatResources.getColorStateList(
                context, getSecondaryIconTintRes(forceLightIconTint));
    }
}
