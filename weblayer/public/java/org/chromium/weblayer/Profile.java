// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer;

import android.os.RemoteException;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.weblayer_private.interfaces.APICallException;
import org.chromium.weblayer_private.interfaces.IProfile;
import org.chromium.weblayer_private.interfaces.ObjectWrapper;

import java.io.File;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/**
 * Profile holds state (typically on disk) needed for browsing. Create a
 * Profile via WebLayer.
 */
public class Profile {
    private static final Map<String, Profile> sProfiles = new HashMap<>();

    /* package */ static Profile of(IProfile impl) {
        ThreadCheck.ensureOnUiThread();
        String name;
        try {
            name = impl.getName();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
        Profile profile = sProfiles.get(name);
        if (profile != null) {
            return profile;
        }

        return new Profile(name, impl);
    }

    /**
     * Return all profiles that have been created and not yet called destroyed.
     * Note returned structure is immutable.
     */
    @NonNull
    public static Collection<Profile> getAllProfiles() {
        ThreadCheck.ensureOnUiThread();
        return Collections.unmodifiableCollection(sProfiles.values());
    }

    private final String mName;
    private IProfile mImpl;

    // Constructor for test mocking.
    protected Profile() {
        mName = null;
        mImpl = null;
    }

    private Profile(String name, IProfile impl) {
        mName = name;
        mImpl = impl;

        sProfiles.put(name, this);
    }

    /**
     * Clears the data associated with the Profile.
     * The clearing is asynchronous, and new data may be generated during clearing. It is safe to
     * call this method repeatedly without waiting for callback.
     *
     * @param dataTypes See {@link BrowsingDataType}.
     * @param fromMillis Defines the start (in milliseconds since epoch) of the time range to clear.
     * @param toMillis Defines the end (in milliseconds since epoch) of the time range to clear.
     * For clearing all data prefer using {@link Long#MAX_VALUE} to
     * {@link System.currentTimeMillis()} to take into account possible system clock changes.
     * @param callback {@link Runnable} which is run when clearing is finished.
     */
    public void clearBrowsingData(@NonNull @BrowsingDataType int[] dataTypes, long fromMillis,
            long toMillis, @NonNull Runnable callback) {
        ThreadCheck.ensureOnUiThread();
        try {
            mImpl.clearBrowsingData(dataTypes, fromMillis, toMillis, ObjectWrapper.wrap(callback));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    /**
     * Clears the data associated with the Profile.
     * Same as {@link #clearBrowsingData(int[], long, long, Runnable)} with unbounded time range.
     */
    public void clearBrowsingData(
            @NonNull @BrowsingDataType int[] dataTypes, @NonNull Runnable callback) {
        ThreadCheck.ensureOnUiThread();
        clearBrowsingData(dataTypes, 0, Long.MAX_VALUE, callback);
    }

    /**
     * Delete all profile data stored on disk. There are a number of edge cases with deleting
     * profile data:
     * * This method will throw an exception if there are any existing usage of this Profile. For
     *   example, all BrowserFragment belonging to this profile must be destroyed.
     * * This object is considered destroyed after this method returns. Calling any other method
     *   after will throw exceptions.
     * * Creating a new profile of the same name before doneCallback runs will throw an exception.
     * @since 82
     */
    public void destroyAndDeleteDataFromDisk(@Nullable Runnable completionCallback) {
        ThreadCheck.ensureOnUiThread();
        if (WebLayer.getSupportedMajorVersionInternal() < 82) {
            throw new UnsupportedOperationException();
        }
        try {
            mImpl.destroyAndDeleteDataFromDisk(ObjectWrapper.wrap(completionCallback));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
        mImpl = null;
        sProfiles.remove(mName);
    }

    @Deprecated
    public void destroy() {
        ThreadCheck.ensureOnUiThread();
        try {
            mImpl.destroy();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
        mImpl = null;
        sProfiles.remove(mName);
    }

    /**
     * Allows embedders to override the default download directory. By default this is the system
     * download directory.
     *
     * @param directory the directory to place downloads in.
     *
     * @since 81
     */
    public void setDownloadDirectory(@NonNull File directory) {
        ThreadCheck.ensureOnUiThread();
        if (WebLayer.getSupportedMajorVersionInternal() < 81) {
            throw new UnsupportedOperationException();
        }

        try {
            mImpl.setDownloadDirectory(directory.toString());
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }
}
