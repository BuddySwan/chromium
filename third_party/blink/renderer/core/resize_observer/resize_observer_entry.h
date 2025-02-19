// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_RESIZE_OBSERVER_RESIZE_OBSERVER_ENTRY_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_RESIZE_OBSERVER_RESIZE_OBSERVER_ENTRY_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/heap/handle.h"

namespace blink {

class Element;
class DOMRectReadOnly;
class LayoutSize;
class ComputedStyle;
class ResizeObserverSize;
class LayoutRect;

class CORE_EXPORT ResizeObserverEntry final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  ResizeObserverEntry(Element* target);

  Element* target() const { return target_; }
  DOMRectReadOnly* contentRect() const { return content_rect_; }
  ResizeObserverSize* contentBoxSize() const { return content_box_size_; }
  ResizeObserverSize* borderBoxSize() const { return border_box_size_; }

  void Trace(Visitor*) override;

 private:
  Member<Element> target_;
  Member<DOMRectReadOnly> content_rect_;
  Member<ResizeObserverSize> content_box_size_;
  Member<ResizeObserverSize> border_box_size_;

  static DOMRectReadOnly* ZoomAdjustedLayoutRect(LayoutRect content_rect,
                                                 const ComputedStyle& style);
  static ResizeObserverSize* ZoomAdjustedSize(const LayoutSize box_size,
                                              const ComputedStyle& style);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_RESIZE_OBSERVER_RESIZE_OBSERVER_ENTRY_H_
