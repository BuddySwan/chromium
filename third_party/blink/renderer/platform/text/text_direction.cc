// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/text/text_direction.h"

#include <ostream>

namespace blink {

std::ostream& operator<<(std::ostream& ostream,
                         base::i18n::TextDirection direction) {
  return ostream << (IsLtr(direction) ? "LTR" : "RTL");
}

}  // namespace blink
