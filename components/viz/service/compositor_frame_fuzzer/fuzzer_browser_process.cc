// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/compositor_frame_fuzzer/fuzzer_browser_process.h"

#include <memory>
#include <utility>

#include "base/run_loop.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/surface_draw_quad.h"
#include "components/viz/common/resources/bitmap_allocation.h"
#include "components/viz/common/surfaces/surface_range.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace viz {

namespace {

// If modifying these sizes, consider also updating the default values in
// compositor_frame_fuzzer.proto to match.
constexpr gfx::Size kBrowserSize(620, 480);
constexpr gfx::Size kTopBarSize(620, 80);
constexpr gfx::Size kRendererFrameSize(620, 400);

constexpr FrameSinkId kEmbeddedFrameSinkId(2, 1);
constexpr FrameSinkId kRootFrameSinkId(1, 1);

}  // namespace

FuzzerBrowserProcess::FuzzerBrowserProcess(
    base::Optional<base::FilePath> png_dir_path)
    : root_local_surface_id_(1, 1, base::UnguessableToken::Create()),
      output_surface_provider_(std::move(png_dir_path)),
      frame_sink_manager_(&shared_bitmap_manager_, &output_surface_provider_) {
  frame_sink_manager_.RegisterFrameSinkId(kEmbeddedFrameSinkId,
                                          /*report_activation=*/false);
  frame_sink_manager_.RegisterFrameSinkId(kRootFrameSinkId,
                                          /*report_activation=*/false);
  frame_sink_manager_.CreateRootCompositorFrameSink(
      BuildRootCompositorFrameSinkParams());
  display_private_->SetDisplayVisible(true);
  display_private_->Resize(kBrowserSize);
}

FuzzerBrowserProcess::~FuzzerBrowserProcess() {
  frame_sink_manager_.InvalidateFrameSinkId(kRootFrameSinkId);
  frame_sink_manager_.InvalidateFrameSinkId(kEmbeddedFrameSinkId);
}

void FuzzerBrowserProcess::EmbedFuzzedCompositorFrame(
    CompositorFrame fuzzed_frame,
    std::vector<FuzzedBitmap> allocated_bitmaps) {
  mojo::Remote<mojom::CompositorFrameSink> sink_remote;
  FakeCompositorFrameSinkClient sink_client;
  frame_sink_manager_.CreateCompositorFrameSink(
      kEmbeddedFrameSinkId, sink_remote.BindNewPipeAndPassReceiver(),
      sink_client.BindInterfaceRemote());

  for (auto& fuzzed_bitmap : allocated_bitmaps) {
    sink_remote->DidAllocateSharedBitmap(
        fuzzed_bitmap.shared_region.Duplicate(), fuzzed_bitmap.id);
  }

  lsi_allocator_.GenerateId();
  SurfaceId embedded_surface_id(
      kEmbeddedFrameSinkId,
      lsi_allocator_.GetCurrentLocalSurfaceIdAllocation().local_surface_id());
  sink_remote->SubmitCompositorFrame(embedded_surface_id.local_surface_id(),
                                     std::move(fuzzed_frame), base::nullopt, 0);

  CompositorFrame browser_frame =
      BuildBrowserUICompositorFrame(embedded_surface_id);
  root_compositor_frame_sink_remote_->SubmitCompositorFrame(
      root_local_surface_id_, std::move(browser_frame), base::nullopt, 0);

  // run queued messages (memory allocation and frame submission)
  base::RunLoop().RunUntilIdle();

  display_private_->ForceImmediateDrawAndSwapIfPossible();

  for (auto& fuzzed_bitmap : allocated_bitmaps) {
    sink_remote->DidDeleteSharedBitmap(fuzzed_bitmap.id);
  }

  // run queued messages (memory deallocation)
  base::RunLoop().RunUntilIdle();

  frame_sink_manager_.DestroyCompositorFrameSink(kEmbeddedFrameSinkId,
                                                 base::DoNothing());
}

mojom::RootCompositorFrameSinkParamsPtr
FuzzerBrowserProcess::BuildRootCompositorFrameSinkParams() {
  auto params = mojom::RootCompositorFrameSinkParams::New();
  params->frame_sink_id = kRootFrameSinkId;
  params->widget = gpu::kNullSurfaceHandle;
  params->gpu_compositing = false;
  params->compositor_frame_sink =
      root_compositor_frame_sink_remote_
          .BindNewEndpointAndPassDedicatedReceiverForTesting();
  params->compositor_frame_sink_client =
      root_compositor_frame_sink_client_.BindInterfaceRemote();
  params->display_private =
      display_private_.BindNewEndpointAndPassDedicatedReceiverForTesting();
  params->display_client = display_client_.BindRemote();
  params->external_begin_frame_controller =
      external_begin_frame_controller_remote_
           .BindNewEndpointAndPassDedicatedReceiverForTesting();
  return params;
}

CompositorFrame FuzzerBrowserProcess::BuildBrowserUICompositorFrame(
    SurfaceId renderer_surface_id) {
  CompositorFrame frame;

  frame.metadata.frame_token = ++next_frame_token_;
  frame.metadata.begin_frame_ack.frame_id = BeginFrameId(
      BeginFrameArgs::kManualSourceId, BeginFrameArgs::kStartingFrameNumber);
  frame.metadata.device_scale_factor = 1;
  frame.metadata.local_surface_id_allocation_time = base::TimeTicks::Now();
  frame.metadata.referenced_surfaces.push_back(
      SurfaceRange(base::nullopt, renderer_surface_id));

  std::unique_ptr<RenderPass> pass = RenderPass::Create();
  pass->SetNew(/*id=*/1, gfx::Rect(kBrowserSize), gfx::Rect(kBrowserSize),
               gfx::Transform());

  auto* renderer_sqs = pass->CreateAndAppendSharedQuadState();
  renderer_sqs->SetAll(gfx::Transform(1.0, 0.0, 0.0, 1.0, 0, 80),
                       gfx::Rect(kRendererFrameSize),
                       gfx::Rect(kRendererFrameSize),
                       /*rounded_corner_bounds=*/gfx::RRectF(),
                       gfx::Rect(kRendererFrameSize), /*is_clipped=*/false,
                       /*are_contents_opaque=*/false, /*opacity=*/1,
                       SkBlendMode::kSrcOver, /*sorting_context_id=*/0);
  auto* surface_quad = pass->CreateAndAppendDrawQuad<SurfaceDrawQuad>();
  surface_quad->SetNew(renderer_sqs, gfx::Rect(kRendererFrameSize),
                       gfx::Rect(kRendererFrameSize),
                       SurfaceRange(base::nullopt, renderer_surface_id),
                       SK_ColorWHITE,
                       /*stretch_content_to_fill_bounds=*/false);

  auto* toolbar_sqs = pass->CreateAndAppendSharedQuadState();
  toolbar_sqs->SetAll(
      gfx::Transform(), gfx::Rect(kTopBarSize), gfx::Rect(kTopBarSize),
      /*rounded_corner_bounds=*/gfx::RRectF(), gfx::Rect(kTopBarSize),
      /*is_clipped=*/false, /*are_contents_opaque=*/false,
      /*opacity=*/1, SkBlendMode::kSrcOver,
      /*sorting_context_id=*/0);
  auto* color_quad = pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  color_quad->SetNew(toolbar_sqs, gfx::Rect(kTopBarSize),
                     gfx::Rect(kTopBarSize), SK_ColorLTGRAY,
                     /*force_antialiasing_off=*/false);
  frame.render_pass_list.push_back(std::move(pass));

  return frame;
}

}  // namespace viz
