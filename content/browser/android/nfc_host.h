// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_NFC_HOST_H_
#define CONTENT_BROWSER_ANDROID_NFC_HOST_H_

#include "base/android/jni_android.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/nfc_provider.mojom.h"
#include "third_party/blink/public/mojom/permissions/permission_status.mojom-forward.h"

namespace content {

class PermissionControllerImpl;

// On Android, NFC requires the Activity associated with the context in order to
// access the NFC system APIs. NFCHost provides this functionality by mapping
// NFC context IDs to the WebContents associated with those IDs.
class NFCHost : public WebContentsObserver {
 public:
  explicit NFCHost(WebContents* web_contents);
  ~NFCHost() override;

  void GetNFC(RenderFrameHost* render_frame_host,
              mojo::PendingReceiver<device::mojom::NFC> receiver);

  // WebContentsObserver implementation.
  void RenderFrameHostChanged(RenderFrameHost* old_host,
                              RenderFrameHost* new_host) override;

 private:
  void OnPermissionStatusChange(blink::mojom::PermissionStatus status);
  void Close();

  // The permission controller for this browser context.
  PermissionControllerImpl* permission_controller_;

  mojo::Remote<device::mojom::NFCProvider> nfc_provider_;

  // Permission change subscription ID provided by |permission_controller_|.
  int subscription_id_ = 0;

  DISALLOW_COPY_AND_ASSIGN(NFCHost);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_NFC_HOST_H_
