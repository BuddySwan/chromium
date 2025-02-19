// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sharing/webrtc/sharing_mojo_service.h"

#include "content/public/browser/service_process_host.h"

namespace sharing {

mojo::PendingRemote<mojom::Sharing> LaunchSharing() {
  mojo::PendingRemote<mojom::Sharing> remote;
  content::ServiceProcessHost::Launch<mojom::Sharing>(
      remote.InitWithNewPipeAndPassReceiver(),
      content::ServiceProcessHost::Options()
          .WithSandboxType(service_manager::SandboxType::kUtility)
          .WithDisplayName("Sharing Service")
          .Pass());
  return remote;
}

}  // namespace sharing
