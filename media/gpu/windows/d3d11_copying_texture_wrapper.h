// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_WINDOWS_D3D11_COPYING_TEXTURE_WRAPPER_H_
#define MEDIA_GPU_WINDOWS_D3D11_COPYING_TEXTURE_WRAPPER_H_

#include <memory>
#include <vector>

#include "media/gpu/media_gpu_export.h"
#include "media/gpu/windows/d3d11_picture_buffer.h"
#include "media/gpu/windows/d3d11_video_processor_proxy.h"

namespace media {

// Uses D3D11VideoProcessor to convert between an input texture2D and an output
// texture2D.  Each instance owns its own destination texture.
class MEDIA_GPU_EXPORT CopyingTexture2DWrapper : public Texture2DWrapper {
 public:
  // |output_wrapper| must wrap a Texture2D which is a single-entry Texture,
  // while |input_texture| may have multiple entries.
  CopyingTexture2DWrapper(const gfx::Size& size,
                          std::unique_ptr<Texture2DWrapper> output_wrapper,
                          std::unique_ptr<VideoProcessorProxy> processor,
                          ComD3D11Texture2D output_texture);
  ~CopyingTexture2DWrapper() override;

  bool ProcessTexture(ComD3D11Texture2D texture,
                      size_t array_slice,
                      MailboxHolderArray* mailbox_dest) override;

  bool Init(GetCommandBufferHelperCB get_helper_cb) override;

 private:
  gfx::Size size_;
  std::unique_ptr<VideoProcessorProxy> video_processor_;
  std::unique_ptr<Texture2DWrapper> output_texture_wrapper_;
  ComD3D11Texture2D output_texture_;
};

}  // namespace media

#endif  // MEDIA_GPU_WINDOWS_D3D11_COPYING_TEXTURE_WRAPPER_H_
