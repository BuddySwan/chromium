// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/paint/paint_artifact.h"

#include "cc/paint/display_item_list.h"
#include "third_party/blink/renderer/platform/geometry/int_rect.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_chunks_to_cc_layer.h"
#include "third_party/blink/renderer/platform/graphics/graphics_layer.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_display_item.h"
#include "third_party/blink/renderer/platform/graphics/paint/geometry_mapper.h"
#include "third_party/blink/renderer/platform/graphics/paint/hit_test_display_item.h"
#include "third_party/blink/renderer/platform/graphics/paint/scroll_hit_test_display_item.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/skia/include/core/SkRegion.h"

namespace blink {

namespace {

static SkColor DisplayItemBackgroundColor(const DisplayItem& item) {
  if (item.GetType() != DisplayItem::kBoxDecorationBackground &&
      item.GetType() != DisplayItem::kDocumentBackground)
    return SK_ColorTRANSPARENT;

  const auto& drawing_item = static_cast<const DrawingDisplayItem&>(item);
  const auto record = drawing_item.GetPaintRecord();
  if (!record)
    return SK_ColorTRANSPARENT;

  for (cc::PaintOpBuffer::Iterator it(record.get()); it; ++it) {
    const auto* op = *it;
    if (op->GetType() == cc::PaintOpType::DrawRect ||
        op->GetType() == cc::PaintOpType::DrawRRect) {
      const auto& flags = static_cast<const cc::PaintOpWithFlags*>(op)->flags;
      // Skip op with looper which may modify the color.
      if (!flags.getLooper() && flags.getStyle() == cc::PaintFlags::kFill_Style)
        return flags.getColor();
    }
  }
  return SK_ColorTRANSPARENT;
}

void ComputeChunkDerivedData(const DisplayItemList& display_items,
                             PaintChunk& chunk) {
  SkRegion known_to_be_opaque_region;
  auto items = display_items.ItemsInPaintChunk(chunk);
  for (const DisplayItem& item : items) {
    chunk.bounds.Unite(item.VisualRect());
    if (item.DrawsContent())
      chunk.drawable_bounds.Unite(item.VisualRect());
    chunk.outset_for_raster_effects = std::max(chunk.outset_for_raster_effects,
                                               item.OutsetForRasterEffects());

    if (RuntimeEnabledFeatures::CompositeAfterPaintEnabled() &&
        item.IsDrawing()) {
      const auto& drawing = static_cast<const DrawingDisplayItem&>(item);
      if (drawing.GetPaintRecord() && drawing.KnownToBeOpaque()) {
        known_to_be_opaque_region.op(SkIRect(drawing.VisualRect()),
                                     SkRegion::kUnion_Op);
      }
    }

    if (item.IsHitTest()) {
      const auto& hit_test = static_cast<const HitTestDisplayItem&>(item);
      if (!chunk.hit_test_data)
        chunk.hit_test_data = std::make_unique<HitTestData>();
      chunk.hit_test_data->AppendTouchActionRect(hit_test.GetHitTestRect());
    }

    // Because ScrollHitTestDisplayItems force new paint chunks (see:
    // PaintChunker::IncrementDisplayItemIndex), they should only be the first
    // item in a paint chunk.
    DCHECK(!item.IsScrollHitTest() || item.Equals(*items.begin()));
  }

  // Because ScrollHitTestDisplayItems force new paint chunks (see:
  // PaintChunker::IncrementDisplayItemIndex), they should only be the first
  // item in a paint chunk.
  if (items.begin()->IsScrollHitTest()) {
    const auto& scroll_hit_test_item =
        static_cast<const ScrollHitTestDisplayItem&>(*items.begin());
    if (!chunk.hit_test_data)
      chunk.hit_test_data = std::make_unique<HitTestData>();
    chunk.hit_test_data->SetScrollHitTest(
        scroll_hit_test_item.scroll_offset_node(),
        scroll_hit_test_item.scroll_container_bounds());
  }

  if (known_to_be_opaque_region.contains(chunk.bounds))
    chunk.known_to_be_opaque = true;
}

// For PaintArtifact::AppendDebugDrawing().
class DebugDrawingClient final : public DisplayItemClient {
 public:
  DebugDrawingClient() { Invalidate(PaintInvalidationReason::kUncacheable); }
  String DebugName() const final { return "DebugDrawing"; }
  IntRect VisualRect() const final { return LayoutRect::InfiniteIntRect(); }
};

}  // namespace

PaintArtifact::PaintArtifact() : display_item_list_(0) {}

PaintArtifact::PaintArtifact(DisplayItemList display_items,
                             Vector<PaintChunk> chunks)
    : display_item_list_(std::move(display_items)), chunks_(std::move(chunks)) {
  for (auto& chunk : chunks_) {
    if (chunk.is_moved_from_cached_subsequence) {
#if DCHECK_IS_ON()
      auto old_bounds = chunk.bounds;
      auto old_drawable_bounds = chunk.drawable_bounds;
      auto old_outset_for_raster_effects = chunk.outset_for_raster_effects;
      auto old_hit_test_data = std::move(chunk.hit_test_data);
      auto old_known_to_be_opaque = chunk.known_to_be_opaque;
      ComputeChunkDerivedData(display_item_list_, chunk);
      DCHECK_EQ(old_bounds, chunk.bounds);
      DCHECK_EQ(old_drawable_bounds, chunk.drawable_bounds);
      DCHECK_EQ(old_outset_for_raster_effects, chunk.outset_for_raster_effects);
      DCHECK((!old_hit_test_data && !chunk.hit_test_data) ||
             (old_hit_test_data && chunk.hit_test_data &&
              *old_hit_test_data == *chunk.hit_test_data));
      DCHECK_EQ(old_known_to_be_opaque, chunk.known_to_be_opaque);
#endif
      continue;
    }
    ComputeChunkDerivedData(display_item_list_, chunk);
  }
}

PaintArtifact::~PaintArtifact() = default;

scoped_refptr<PaintArtifact> PaintArtifact::Create(
    DisplayItemList display_items,
    Vector<PaintChunk> chunks) {
  return base::AdoptRef(
      new PaintArtifact(std::move(display_items), std::move(chunks)));
}

scoped_refptr<PaintArtifact> PaintArtifact::Empty() {
  DEFINE_STATIC_REF(PaintArtifact, empty, base::AdoptRef(new PaintArtifact()));
  return empty;
}

size_t PaintArtifact::ApproximateUnsharedMemoryUsage() const {
  size_t total_size = sizeof(*this) + display_item_list_.MemoryUsageInBytes() +
                      chunks_.capacity() * sizeof(chunks_[0]);
  for (const auto& chunk : chunks_)
    total_size += chunk.MemoryUsageInBytes();
  return total_size;
}

void PaintArtifact::AppendDebugDrawing(
    sk_sp<const PaintRecord> record,
    const PropertyTreeState& property_tree_state) {
  DEFINE_STATIC_LOCAL(DebugDrawingClient, debug_drawing_client, ());

  DCHECK(!RuntimeEnabledFeatures::CompositeAfterPaintEnabled());
  auto& display_item =
      display_item_list_.AllocateAndConstruct<DrawingDisplayItem>(
          debug_drawing_client, DisplayItem::kDebugDrawing, std::move(record));

  // Create a PaintChunk for the debug drawing.
  chunks_.emplace_back(display_item_list_.size() - 1, display_item_list_.size(),
                       display_item.GetId(), property_tree_state);
  ComputeChunkDerivedData(display_item_list_, chunks_.back());
}

void PaintArtifact::Replay(GraphicsContext& graphics_context,
                           const PropertyTreeState& replay_state,
                           const IntPoint& offset) const {
  Replay(*graphics_context.Canvas(), replay_state, offset);
}

void PaintArtifact::Replay(cc::PaintCanvas& canvas,
                           const PropertyTreeState& replay_state,
                           const IntPoint& offset) const {
  TRACE_EVENT0("blink,benchmark", "PaintArtifact::replay");
  canvas.drawPicture(GetPaintRecord(replay_state, offset));
}

sk_sp<PaintRecord> PaintArtifact::GetPaintRecord(
    const PropertyTreeState& replay_state,
    const IntPoint& offset) const {
  return PaintChunksToCcLayer::Convert(
             PaintChunks(), replay_state,
             gfx::Vector2dF(offset.X(), offset.Y()), GetDisplayItemList(),
             cc::DisplayItemList::kToBeReleasedAsPaintOpBuffer)
      ->ReleaseAsRecord();
}

SkColor PaintArtifact::SafeOpaqueBackgroundColor(
    const PaintChunkSubset& chunks) const {
  // Find the background color from the first drawable display item.
  for (const auto& chunk : chunks) {
    for (const auto& item : display_item_list_.ItemsInPaintChunk(chunk)) {
      if (item.DrawsContent())
        return DisplayItemBackgroundColor(item);
    }
  }
  return SK_ColorTRANSPARENT;
}

void PaintArtifact::FinishCycle() {
  // Until CompositeAfterPaint, PaintController::ClearPropertyTreeChangedStateTo
  // is used for clearing the property tree changed state at the end of paint
  // instead of in FinishCycle. See: LocalFrameView::RunPaintLifecyclePhase.
  bool clear_property_tree_changed =
      RuntimeEnabledFeatures::CompositeAfterPaintEnabled();
  for (auto& chunk : chunks_) {
    chunk.client_is_just_created = false;
    if (clear_property_tree_changed)
      chunk.properties.ClearChangedToRoot();
  }
}

}  // namespace blink
