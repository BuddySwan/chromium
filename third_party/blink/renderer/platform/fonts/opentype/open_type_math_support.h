// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_FONTS_OPENTYPE_OPEN_TYPE_MATH_SUPPORT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_FONTS_OPENTYPE_OPEN_TYPE_MATH_SUPPORT_H_

#include "base/optional.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

class HarfBuzzFace;

// TODO(https://crbug.com/1050596): Add unit test.
class PLATFORM_EXPORT OpenTypeMathSupport {
 public:
  static bool HasMathData(const HarfBuzzFace*);

  // These constants are defined in the OpenType MATH table:
  // https://docs.microsoft.com/en-us/typography/opentype/spec/math#mathconstants-table
  // Their values match the indices in the MathConstants subtable.
  enum MathConstants {
    kScriptPercentScaleDown = 0,
    kScriptScriptPercentScaleDown = 1,
    kDelimitedSubFormulaMinHeight = 2,
    kDisplayOperatorMinHeight = 3,
    kMathLeading = 4,
    kAxisHeight = 5,
    kAccentBaseHeight = 6,
    kFlattenedAccentBaseHeight = 7,
    kSubscriptShiftDown = 8,
    kSubscriptTopMax = 9,
    kSubscriptBaselineDropMin = 10,
    kSuperscriptShiftUp = 11,
    kSuperscriptShiftUpCramped = 12,
    kSuperscriptBottomMin = 13,
    kSuperscriptBaselineDropMax = 14,
    kSubSuperscriptGapMin = 15,
    kSuperscriptBottomMaxWithSubscript = 16,
    kSpaceAfterScript = 17,
    kUpperLimitGapMin = 18,
    kUpperLimitBaselineRiseMin = 19,
    kLowerLimitGapMin = 20,
    kLowerLimitBaselineDropMin = 21,
    kStackTopShiftUp = 22,
    kStackTopDisplayStyleShiftUp = 23,
    kStackBottomShiftDown = 24,
    kStackBottomDisplayStyleShiftDown = 25,
    kStackGapMin = 26,
    kStackDisplayStyleGapMin = 27,
    kStretchStackTopShiftUp = 28,
    kStretchStackBottomShiftDown = 29,
    kStretchStackGapAboveMin = 30,
    kStretchStackGapBelowMin = 31,
    kFractionNumeratorShiftUp = 32,
    kFractionNumeratorDisplayStyleShiftUp = 33,
    kFractionDenominatorShiftDown = 34,
    kFractionDenominatorDisplayStyleShiftDown = 35,
    kFractionNumeratorGapMin = 36,
    kFractionNumDisplayStyleGapMin = 37,
    kFractionRuleThickness = 38,
    kFractionDenominatorGapMin = 39,
    kFractionDenomDisplayStyleGapMin = 40,
    kSkewedFractionHorizontalGap = 41,
    kSkewedFractionVerticalGap = 42,
    kOverbarVerticalGap = 43,
    kOverbarRuleThickness = 44,
    kOverbarExtraAscender = 45,
    kUnderbarVerticalGap = 46,
    kUnderbarRuleThickness = 47,
    kUnderbarExtraDescender = 48,
    kRadicalVerticalGap = 49,
    kRadicalDisplayStyleVerticalGap = 50,
    kRadicalRuleThickness = 51,
    kRadicalExtraAscender = 52,
    kRadicalKernBeforeDegree = 53,
    kRadicalKernAfterDegree = 54,
    kRadicalDegreeBottomRaisePercent = 55
  };

  static base::Optional<float> MathConstant(const HarfBuzzFace*, MathConstants);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_FONTS_OPENTYPE_OPEN_TYPE_MATH_SUPPORT_H_
