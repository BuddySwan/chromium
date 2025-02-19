// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sharing/webrtc/webrtc_flags.h"

const base::Feature kSharingPeerConnectionReceiver{
    "SharingPeerConnectionReceiver", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSharingPeerConnectionSender{
    "SharingPeerConnectionSender", base::FEATURE_ENABLED_BY_DEFAULT};
