// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill_assistant.carousel;

import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.List;

/**
 * State for the carousel of the Autofill Assistant.
 */
public class AssistantCarouselModel extends PropertyModel {
    public static final WritableObjectPropertyKey<List<AssistantChip>> CHIPS =
            new WritableObjectPropertyKey<>();

    public AssistantCarouselModel() {
        super(CHIPS);
        set(CHIPS, new ArrayList<>());
    }

    public void setChips(List<AssistantChip> chips) {
        set(CHIPS, chips);
    }
}
