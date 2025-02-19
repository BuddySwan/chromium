// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_DOCUMENT_ANIMATION_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_DOCUMENT_ANIMATION_H_

#include "third_party/blink/renderer/core/animation/animation.h"
#include "third_party/blink/renderer/core/animation/document_animations.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class DocumentAnimation {
  STATIC_ONLY(DocumentAnimation);

 public:
  static DocumentTimeline* timeline(Document& document) {
    return &document.Timeline();
  }

  static HeapVector<Member<Animation>> getAnimations(Document& document) {
    return document.GetDocumentAnimations().getAnimations();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_DOCUMENT_ANIMATION_H_
