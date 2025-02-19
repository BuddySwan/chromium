// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_SURFACE_DEPENDENCY_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_SURFACE_DEPENDENCY_H_

#include <vector>

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/service/sequence_id.h"
#include "gpu/ipc/common/surface_handle.h"
#include "ui/gl/gl_surface_format.h"

class GURL;

namespace gl {
class GLSurface;
}

namespace gpu {

class DisplayContext;
class GpuDriverBugWorkarounds;
class ImageFactory;
class ImageTransportSurfaceDelegate;
class MailboxManager;
class SharedContextState;
class SharedImageManager;
class SingleTaskSequence;
class SyncPointManager;
struct GpuFeatureInfo;
struct GpuPreferences;

namespace raster {
class GrShaderCache;
}

}  // namespace gpu

namespace viz {

class DawnContextProvider;
class VulkanContextProvider;

// This class exists to allow SkiaOutputSurfaceImpl to ignore differences
// between Android Webview and regular viz environment.
// Note generally this class only provides access to, but does not own any of
// the objects it returns raw pointers to. Embedders need to ensure these
// objects remain in scope while SkiaOutputSurface is alive. SkiaOutputSurface
// similarly should not hold onto these objects beyond its own lifetime.
class VIZ_SERVICE_EXPORT SkiaOutputSurfaceDependency {
 public:
  virtual ~SkiaOutputSurfaceDependency() = default;

  // These are client thread methods. All other methods should be called on
  // the GPU thread only.
  virtual bool IsUsingVulkan() = 0;
  virtual bool IsUsingDawn() = 0;
  // Returns a new task execution sequence. Sequences should not outlive the
  // task executor.
  virtual std::unique_ptr<gpu::SingleTaskSequence> CreateSequence() = 0;

  virtual gpu::SharedImageManager* GetSharedImageManager() = 0;
  virtual gpu::SyncPointManager* GetSyncPointManager() = 0;
  virtual const gpu::GpuDriverBugWorkarounds& GetGpuDriverBugWorkarounds() = 0;
  virtual scoped_refptr<gpu::SharedContextState> GetSharedContextState() = 0;
  // May return null.
  virtual gpu::raster::GrShaderCache* GetGrShaderCache() = 0;
  // May return null.
  virtual VulkanContextProvider* GetVulkanContextProvider() = 0;
  // May return null.
  virtual DawnContextProvider* GetDawnContextProvider() = 0;
  virtual const gpu::GpuPreferences& GetGpuPreferences() = 0;
  virtual const gpu::GpuFeatureInfo& GetGpuFeatureInfo() = 0;
  virtual gpu::MailboxManager* GetMailboxManager() = 0;
  // May return null.
  virtual gpu::ImageFactory* GetGpuImageFactory() = 0;
  // Note it is possible for IsOffscreen to be false and GetSurfaceHandle to
  // return kNullSurfaceHandle.
  virtual bool IsOffscreen() = 0;
  virtual gpu::SurfaceHandle GetSurfaceHandle() = 0;
  virtual scoped_refptr<gl::GLSurface> CreateGLSurface(
      base::WeakPtr<gpu::ImageTransportSurfaceDelegate> stub,
      gl::GLSurfaceFormat format) = 0;
  // Hold a ref of the given surface until the returned closure is fired.
  virtual base::ScopedClosureRunner CacheGLSurface(gl::GLSurface* surface) = 0;
  virtual void PostTaskToClientThread(base::OnceClosure closure) = 0;
  virtual void ScheduleGrContextCleanup() = 0;

  // This function schedules delayed task to be run on GPUThread. It can be
  // called only from GPU Thread.
  virtual void ScheduleDelayedGPUTaskFromGPUThread(base::OnceClosure task) = 0;

#if defined(OS_WIN)
  virtual void DidCreateAcceleratedSurfaceChildWindow(
      gpu::SurfaceHandle parent_window,
      gpu::SurfaceHandle child_window) = 0;
#endif

  virtual void RegisterDisplayContext(gpu::DisplayContext* display_context) = 0;
  virtual void UnregisterDisplayContext(
      gpu::DisplayContext* display_context) = 0;
  virtual void DidLoseContext(bool offscreen,
                              gpu::error::ContextLostReason reason,
                              const GURL& active_url) = 0;

  virtual base::TimeDelta GetGpuBlockedTimeSinceLastSwap() = 0;
  virtual bool NeedsSupportForExternalStencil() = 0;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_SURFACE_DEPENDENCY_H_
