// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/ng_physical_container_fragment.h"

#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_inline.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_container_fragment_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_outline_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"

namespace blink {

namespace {

struct SameSizeAsNGPhysicalContainerFragment : NGPhysicalFragment {
  void* break_token;
  std::unique_ptr<Vector<NGPhysicalOutOfFlowPositionedNode>>
      oof_positioned_descendants_;
  void* pointer;
  wtf_size_t size;
};

static_assert(sizeof(NGPhysicalContainerFragment) ==
                  sizeof(SameSizeAsNGPhysicalContainerFragment),
              "NGPhysicalContainerFragment should stay small");

}  // namespace

NGPhysicalContainerFragment::NGPhysicalContainerFragment(
    NGContainerFragmentBuilder* builder,
    WritingMode block_or_line_writing_mode,
    NGLink* buffer,
    NGFragmentType type,
    unsigned sub_type)
    : NGPhysicalFragment(builder, type, sub_type),
      break_token_(std::move(builder->break_token_)),
      oof_positioned_descendants_(
          builder->oof_positioned_descendants_.IsEmpty()
              ? nullptr
              : new Vector<NGPhysicalOutOfFlowPositionedNode>()),
      buffer_(buffer),
      num_children_(builder->children_.size()) {
  has_floating_descendants_for_paint_ =
      builder->has_floating_descendants_for_paint_;
  has_adjoining_object_descendants_ =
      builder->has_adjoining_object_descendants_;
  has_orthogonal_flow_roots_ = builder->has_orthogonal_flow_roots_;
  may_have_descendant_above_block_start_ =
      builder->may_have_descendant_above_block_start_;
  depends_on_percentage_block_size_ = DependsOnPercentageBlockSize(*builder);

  PhysicalSize size = Size();
  if (oof_positioned_descendants_) {
    oof_positioned_descendants_->ReserveCapacity(
        builder->oof_positioned_descendants_.size());
    for (const auto& descendant : builder->oof_positioned_descendants_) {
      oof_positioned_descendants_->emplace_back(
          descendant.node,
          descendant.static_position.ConvertToPhysical(
              builder->Style().GetWritingMode(), builder->Direction(), size),
          descendant.inline_container);
    }
  }

  // Because flexible arrays need to be the last member in a class, we need to
  // have the buffer passed as a constructor argument and have the actual
  // storage be part of the subclass.
  wtf_size_t i = 0;
  for (auto& child : builder->children_) {
    buffer[i].fragment = child.fragment.get();
    buffer[i].fragment->AddRef();
    buffer[i].offset = child.offset.ConvertToPhysical(
        block_or_line_writing_mode, builder->Direction(), size,
        child.fragment->Size());
    ++i;
  }
}

NGPhysicalContainerFragment::~NGPhysicalContainerFragment() = default;

// additional_offset must be offset from the containing_block.
void NGPhysicalContainerFragment::AddOutlineRectsForNormalChildren(
    Vector<PhysicalRect>* outline_rects,
    const PhysicalOffset& additional_offset,
    NGOutlineType outline_type,
    const LayoutBoxModelObject* containing_block) const {
  for (const auto& child : PostLayoutChildren()) {
    // Outlines of out-of-flow positioned descendants are handled in
    // NGPhysicalBoxFragment::AddSelfOutlineRects().
    if (child->IsOutOfFlowPositioned())
      continue;

    // Outline of an element continuation or anonymous block continuation is
    // added when we iterate the continuation chain.
    // See NGPhysicalBoxFragment::AddSelfOutlineRects().
    if (!child->IsLineBox()) {
      const LayoutObject* child_layout_object = child->GetLayoutObject();
      if (auto* child_layout_block_flow =
              DynamicTo<LayoutBlockFlow>(child_layout_object)) {
        if (child_layout_object->IsElementContinuation() ||
            child_layout_block_flow->IsAnonymousBlockContinuation())
          continue;
      }
    }
    AddOutlineRectsForDescendant(child, outline_rects, additional_offset,
                                 outline_type, containing_block);
  }
}

// additional_offset must be offset from the containing_block because
// LocalToAncestorRect returns rects wrt containing_block.
void NGPhysicalContainerFragment::AddOutlineRectsForDescendant(
    const NGLink& descendant,
    Vector<PhysicalRect>* outline_rects,
    const PhysicalOffset& additional_offset,
    NGOutlineType outline_type,
    const LayoutBoxModelObject* containing_block) const {
  if (descendant->IsText() || descendant->IsListMarker())
    return;

  if (const auto* descendant_box =
          DynamicTo<NGPhysicalBoxFragment>(descendant.get())) {
    const LayoutObject* descendant_layout_object =
        descendant_box->GetLayoutObject();

    // TODO(layoutng): Explain this check. I assume we need it because layers
    // may have transforms and so we have to go through LocalToAncestorRects?
    if (descendant_box->HasLayer()) {
      DCHECK(descendant_layout_object);
      Vector<PhysicalRect> layer_outline_rects;
      descendant_box->AddSelfOutlineRects(PhysicalOffset(), outline_type,
                                          &layer_outline_rects);

      // Don't pass additional_offset because LocalToAncestorRects will itself
      // apply it.
      descendant_layout_object->LocalToAncestorRects(
          layer_outline_rects, containing_block, PhysicalOffset(),
          PhysicalOffset());
      outline_rects->AppendVector(layer_outline_rects);
      return;
    }

    if (!descendant_box->IsInlineBox()) {
      descendant_box->AddSelfOutlineRects(
          additional_offset + descendant.Offset(), outline_type, outline_rects);
      return;
    }

    DCHECK(descendant_layout_object);
    DCHECK(descendant_layout_object->IsLayoutInline());
    const LayoutInline* descendant_layout_inline =
        ToLayoutInline(descendant_layout_object);
    // As an optimization, an ancestor has added rects for its line boxes
    // covering descendants' line boxes, so descendants don't need to add line
    // boxes again. For example, if the parent is a LayoutBlock, it adds rects
    // for its line box which cover the line boxes of this LayoutInline. So
    // the LayoutInline needs to add rects for children and continuations
    // only.
    if (NGOutlineUtils::ShouldPaintOutline(*descendant_box)) {
      // For inline fragments, don't accumulate |descendant.Offset()| because
      // offsets are relative to its inline formatting context.
      descendant_layout_inline->AddOutlineRectsForChildrenAndContinuations(
          *outline_rects, additional_offset, outline_type);
    }
    return;
  }

  if (const auto* descendant_line_box =
          DynamicTo<NGPhysicalLineBoxFragment>(descendant.get())) {
    // For inline fragments, don't accumulate |descendant.Offset()| because
    // offsets are relative to its inline formatting context.
    descendant_line_box->AddOutlineRectsForNormalChildren(
        outline_rects, additional_offset, outline_type, containing_block);

    if (!descendant_line_box->Size().IsEmpty()) {
      outline_rects->emplace_back(additional_offset,
                                  descendant_line_box->Size().ToLayoutSize());
    } else if (descendant_line_box->Children().empty()) {
      // Special-case for when the first continuation does not generate
      // fragments. NGInlineLayoutAlgorithm suppresses box fragments when the
      // line is "empty". When there is a continuation from the LayoutInline,
      // the suppression makes such continuation not reachable. Check the
      // continuation from LayoutInline in such case.
      DCHECK(GetLayoutObject());
      if (LayoutInline* first_layout_inline =
              ToLayoutInlineOrNull(GetLayoutObject()->SlowFirstChild())) {
        if (!first_layout_inline->IsElementContinuation()) {
          first_layout_inline->AddOutlineRectsForChildrenAndContinuations(
              *outline_rects, additional_offset, outline_type);
        }
      }
    }
  }
}

bool NGPhysicalContainerFragment::DependsOnPercentageBlockSize(
    const NGContainerFragmentBuilder& builder) {
  NGLayoutInputNode node = builder.node_;

  if (!node || node.IsInline())
    return builder.has_descendant_that_depends_on_percentage_block_size_;

  // NOTE: If an element is OOF positioned, and has top/bottom constraints
  // which are percentage based, this function will return false.
  //
  // This is fine as the top/bottom constraints are computed *before* layout,
  // and the result is set as a fixed-block-size constraint. (And the caching
  // logic will never check the result of this function).
  //
  // The result of this function still may be used for an OOF positioned
  // element if it has a percentage block-size however, but this will return
  // the correct result from below.

  // There are two conditions where we need to know about an (arbitrary)
  // descendant which depends on a %-block-size.
  //  - In quirks mode, the arbitrary descendant may depend the percentage
  //    resolution block-size given (to this node), and need to relayout if
  //    this size changes.
  //  - A flex-item may have its "definiteness" change, (e.g. if itself is a
  //    flex item which is being stretched). This definiteness change will
  //    affect any %-block-size children.
  //
  // NOTE(ikilpatrick): For the flex-item case this is potentially too general.
  // We only need to know about if this flex-item has a %-block-size child if
  // the "definiteness" changes, not if the percentage resolution size changes.
  if ((builder.has_descendant_that_depends_on_percentage_block_size_ ||
       builder.is_legacy_layout_root_) &&
      (node.UseParentPercentageResolutionBlockSizeForChildren() ||
       node.IsFlexItem()))
    return true;

  const ComputedStyle& style = builder.Style();
  if (style.LogicalHeight().IsPercentOrCalc() ||
      style.LogicalMinHeight().IsPercentOrCalc() ||
      style.LogicalMaxHeight().IsPercentOrCalc())
    return true;

  return false;
}

}  // namespace blink
