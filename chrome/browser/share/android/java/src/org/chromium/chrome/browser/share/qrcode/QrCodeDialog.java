// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.share.qrcode;

import android.app.Dialog;
import android.app.DialogFragment;
import android.content.Context;
import android.os.Bundle;
import android.support.design.widget.TabLayout;
import android.support.v4.view.ViewPager;
import android.support.v7.app.AlertDialog;
import android.view.View;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.share.qrcode.scan_tab.QrCodeScanCoordinator;
import org.chromium.chrome.browser.share.qrcode.share_tab.QrCodeShareCoordinator;
import org.chromium.ui.widget.ChromeImageButton;

import java.util.ArrayList;

/**
 * QrCodeDialog is the main view for QR code sharing and scanning.
 */
public class QrCodeDialog extends DialogFragment {
    private ArrayList<QrCodeDialogTab> mTabs;
    private Context mContext;

    /**
     * The QrCodeDialog constructor.
     */
    public QrCodeDialog() {
        mTabs = new ArrayList<QrCodeDialogTab>();
    }

    @Override
    public void onAttach(Context context) {
        super.onAttach(context);
        mContext = context;

        QrCodeShareCoordinator shareCoordinator = new QrCodeShareCoordinator(context);
        QrCodeScanCoordinator scanCoordinator = new QrCodeScanCoordinator(context, this::dismiss);

        mTabs.add(shareCoordinator);
        mTabs.add(scanCoordinator);
    }

    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        AlertDialog.Builder builder =
                new AlertDialog.Builder(getActivity(), R.style.Theme_Chromium_Fullscreen);
        builder.setView(getDialogView());
        return builder.create();
    }

    @Override
    public void onResume() {
        super.onResume();
        for (QrCodeDialogTab tab : mTabs) {
            tab.onResume();
        }
    }
    @Override
    public void onPause() {
        super.onPause();
        for (QrCodeDialogTab tab : mTabs) {
            tab.onPause();
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        mContext = null;
        for (QrCodeDialogTab tab : mTabs) {
            tab.onDestroy();
        }
        mTabs.clear();
    }

    private View getDialogView() {
        View dialogView = (View) getActivity().getLayoutInflater().inflate(
                org.chromium.chrome.browser.share.qrcode.R.layout.qrcode_dialog, null);
        ChromeImageButton closeButton =
                (ChromeImageButton) dialogView.findViewById(R.id.close_button);
        closeButton.setOnClickListener(v -> dismiss());

        // Setup page adapter and tab layout.
        ArrayList<View> pages = new ArrayList<View>();
        for (QrCodeDialogTab tab : mTabs) {
            pages.add(tab.getView());
        }
        QrCodePageAdapter pageAdapter = new QrCodePageAdapter(pages);

        TabLayout tabLayout =
                dialogView.findViewById(org.chromium.chrome.browser.share.qrcode.R.id.tab_layout);
        ViewPager viewPager = dialogView.findViewById(
                org.chromium.chrome.browser.share.qrcode.R.id.qrcode_view_pager);
        viewPager.setAdapter(pageAdapter);
        viewPager.addOnPageChangeListener(new TabLayout.TabLayoutOnPageChangeListener(tabLayout));
        tabLayout.addOnTabSelectedListener(new TabLayout.ViewPagerOnTabSelectedListener(viewPager));
        return dialogView;
    }
}