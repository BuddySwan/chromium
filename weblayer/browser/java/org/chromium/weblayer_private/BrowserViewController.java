// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer_private;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.webkit.ValueCallback;
import android.widget.FrameLayout;
import android.widget.RelativeLayout;

import org.chromium.base.annotations.JNINamespace;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;

/**
 * BrowserViewController controls the set of Views needed to show the WebContents.
 */
@JNINamespace("weblayer")
public final class BrowserViewController
        implements TopControlsContainerView.Listener,
                   WebContentsGestureStateTracker.OnGestureStateChangedListener {
    private final ContentViewRenderView mContentViewRenderView;
    private final ContentView mContentView;
    // Child of mContentView, holds top-view from client.
    private final TopControlsContainerView mTopControlsContainerView;
    // Other child of mContentView, which holds views that sit on top of the web contents, such as
    // tab modal dialogs.
    private final FrameLayout mWebContentsOverlayView;

    private final ModalDialogManager mModalDialogManager;

    private TabImpl mTab;

    private WebContentsGestureStateTracker mGestureStateTracker;

    /**
     * The value of mCachedDoBrowserControlsShrinkRendererSize is set when
     * WebContentsGestureStateTracker begins a gesture. This is necessary as the values should only
     * change once a gesture is no longer under way.
     */
    private boolean mCachedDoBrowserControlsShrinkRendererSize;

    public BrowserViewController(Context context, WindowAndroid windowAndroid) {
        mContentViewRenderView = new ContentViewRenderView(context);

        mContentViewRenderView.onNativeLibraryLoaded(
                windowAndroid, ContentViewRenderView.MODE_SURFACE_VIEW);
        mTopControlsContainerView =
                new TopControlsContainerView(context, mContentViewRenderView, this);
        mTopControlsContainerView.setId(View.generateViewId());
        mContentView = ContentView.createContentView(
                context, mTopControlsContainerView.getEventOffsetHandler());
        mContentViewRenderView.addView(mContentView,
                new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.UNSPECIFIED_GRAVITY));
        mContentView.addView(mTopControlsContainerView,
                new RelativeLayout.LayoutParams(
                        LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        mWebContentsOverlayView = new FrameLayout(context);
        RelativeLayout.LayoutParams overlayParams =
                new RelativeLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0);
        overlayParams.addRule(RelativeLayout.BELOW, mTopControlsContainerView.getId());
        overlayParams.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        mContentView.addView(mWebContentsOverlayView, overlayParams);
        windowAndroid.setAnimationPlaceholderView(mWebContentsOverlayView);

        mModalDialogManager = windowAndroid.getModalDialogManager();
        mModalDialogManager.registerPresenter(
                new WebLayerTabModalPresenter(this, context), ModalDialogType.TAB);
    }

    public void destroy() {
        setActiveTab(null);
        mTopControlsContainerView.destroy();
        mContentViewRenderView.destroy();
    }

    /** Returns top-level View this Controller works with */
    public View getView() {
        return mContentViewRenderView;
    }

    public ViewGroup getContentView() {
        return mContentView;
    }

    public FrameLayout getWebContentsOverlayView() {
        return mWebContentsOverlayView;
    }

    public void setActiveTab(TabImpl tab) {
        if (tab == mTab) return;

        if (mTab != null) {
            mTab.onDidLoseActive();
            // WebContentsGestureStateTracker is relatively cheap, easier to destroy rather than
            // update WebContents.
            mGestureStateTracker.destroy();
            mGestureStateTracker = null;
        }
        mTab = tab;
        WebContents webContents = mTab != null ? mTab.getWebContents() : null;
        // Create the WebContentsGestureStateTracker before setting the WebContents on
        // the views as they may call back to this class.
        if (mTab != null) {
            mGestureStateTracker =
                    new WebContentsGestureStateTracker(mContentView, webContents, this);
        }
        mContentView.setWebContents(webContents);

        if (mTab != null) {
            // Now that |mContentView| is associated with this Tab's WebContents,
            // associate |mContentView| with this Tab's AutofillProvider as well.
            mContentView.setAutofillProvider(mTab.getAutofillProvider());
        } else {
            mContentView.setAutofillProvider(null);
        }

        mContentViewRenderView.setWebContents(webContents);
        mTopControlsContainerView.setWebContents(webContents);
        if (mTab != null) {
            mTab.onDidGainActive(mTopControlsContainerView.getNativeHandle());
            mContentView.requestFocus();
        }

        mModalDialogManager.dismissDialogsOfType(
                ModalDialogType.TAB, DialogDismissalCause.TAB_SWITCHED);
    }

    public TabImpl getTab() {
        return mTab;
    }

    public void setTopView(View view) {
        mTopControlsContainerView.setView(view);
    }

    @Override
    public void onTopControlsCompletelyShownOrHidden() {
        adjustWebContentsHeightIfNecessary();
    }

    @Override
    public void onGestureStateChanged() {
        if (mGestureStateTracker.isInGestureOrScroll()) {
            mCachedDoBrowserControlsShrinkRendererSize =
                    mTopControlsContainerView.isTopControlVisible();
        }
        adjustWebContentsHeightIfNecessary();
    }

    private void adjustWebContentsHeightIfNecessary() {
        if (mGestureStateTracker.isInGestureOrScroll()
                || !mTopControlsContainerView.isTopControlsCompletelyShownOrHidden()) {
            return;
        }
        mContentViewRenderView.setWebContentsHeightDelta(
                mTopControlsContainerView.getTopContentOffset());
    }

    public void setSupportsEmbedding(boolean enable, ValueCallback<Boolean> callback) {
        mContentViewRenderView.requestMode(enable ? ContentViewRenderView.MODE_TEXTURE_VIEW
                                                  : ContentViewRenderView.MODE_SURFACE_VIEW,
                callback);
    }

    public void onTopControlsChanged(int topControlsOffsetY, int topContentOffsetY) {
        mTopControlsContainerView.onTopControlsChanged(topControlsOffsetY, topContentOffsetY);
    }

    public boolean doBrowserControlsShrinkRendererSize() {
        return (mGestureStateTracker.isInGestureOrScroll())
                ? mCachedDoBrowserControlsShrinkRendererSize
                : mTopControlsContainerView.isTopControlVisible();
    }
}
