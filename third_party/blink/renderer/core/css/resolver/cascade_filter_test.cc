// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/resolver/cascade_filter.h"
#include <gtest/gtest.h>

namespace blink {

TEST(CascadeFilterTest, FilterNothing) {
  CascadeFilter filter;
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyBackgroundColor()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyColor()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyDisplay()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyFloat()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyInternalVisitedColor()));
}

TEST(CascadeFilterTest, ConstructorBehavesLikeSingleAdd) {
  EXPECT_EQ(CascadeFilter().Add(CSSProperty::kInherited, true),
            CascadeFilter(CSSProperty::kInherited, true));
  EXPECT_EQ(CascadeFilter().Add(CSSProperty::kInherited, false),
            CascadeFilter(CSSProperty::kInherited, false));
}

TEST(CascadeFilterTest, Equals) {
  EXPECT_EQ(CascadeFilter(CSSProperty::kInherited, true),
            CascadeFilter(CSSProperty::kInherited, true));
  EXPECT_EQ(CascadeFilter(CSSProperty::kInherited, false),
            CascadeFilter(CSSProperty::kInherited, false));
}

TEST(CascadeFilterTest, NotEqualsMask) {
  EXPECT_NE(CascadeFilter(CSSProperty::kInherited, true),
            CascadeFilter(CSSProperty::kInherited, false));
  EXPECT_NE(CascadeFilter(CSSProperty::kInherited, false),
            CascadeFilter(CSSProperty::kVisited, false));
  EXPECT_NE(CascadeFilter(CSSProperty::kInherited, false),
            CascadeFilter(CSSProperty::kInherited, false)
                .Add(CSSProperty::kVisited, false));
  EXPECT_NE(CascadeFilter(CSSProperty::kInherited, false), CascadeFilter());
}

TEST(CascadeFilterTest, FilterInherited) {
  CascadeFilter filter(CSSProperty::kInherited, true);
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyBackgroundColor()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyColor()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyFontSize()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyDisplay()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyFloat()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyInternalVisitedColor()));
}

TEST(CascadeFilterTest, FilterNonInherited) {
  CascadeFilter filter(CSSProperty::kInherited, false);
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyBackgroundColor()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyColor()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyDisplay()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyFloat()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyInternalVisitedColor()));
}

TEST(CascadeFilterTest, FilterVisitedAndInherited) {
  auto filter = CascadeFilter()
                    .Add(CSSProperty::kVisited, true)
                    .Add(CSSProperty::kInherited, true);
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyBackgroundColor()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyColor()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyDisplay()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyFloat()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyInternalVisitedBackgroundColor()));
}

TEST(CascadeFilterTest, FilterVisitedAndNonInherited) {
  auto filter = CascadeFilter()
                    .Add(CSSProperty::kVisited, true)
                    .Add(CSSProperty::kInherited, false);
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyBackgroundColor()));
  EXPECT_FALSE(filter.Rejects(GetCSSPropertyColor()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyDisplay()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyFloat()));
  EXPECT_TRUE(filter.Rejects(GetCSSPropertyInternalVisitedColor()));
}

TEST(CascadeFilterTest, RejectFlag) {
  auto filter = CascadeFilter()
                    .Add(CSSProperty::kVisited, true)
                    .Add(CSSProperty::kInherited, false);
  EXPECT_TRUE(filter.Rejects(CSSProperty::kVisited, true));
  EXPECT_TRUE(filter.Rejects(CSSProperty::kInherited, false));
  EXPECT_FALSE(filter.Rejects(CSSProperty::kUA, true));
  EXPECT_FALSE(filter.Rejects(CSSProperty::kUA, false));
  EXPECT_FALSE(filter.Rejects(CSSProperty::kVisited, false));
  EXPECT_FALSE(filter.Rejects(CSSProperty::kInherited, true));
}

TEST(CascadeFilterTest, AddDoesNotOverwrite) {
  auto filter = CascadeFilter()
                    .Add(CSSProperty::kVisited, true)
                    .Add(CSSProperty::kInherited, false);
  EXPECT_TRUE(filter.Rejects(CSSProperty::kVisited, true));
  EXPECT_TRUE(filter.Rejects(CSSProperty::kInherited, false));
  filter = filter.Add(CSSProperty::kVisited, false);
  filter = filter.Add(CSSProperty::kInherited, true);
  // Add has no effect if flags are already set:
  EXPECT_TRUE(filter.Rejects(CSSProperty::kVisited, true));
  EXPECT_TRUE(filter.Rejects(CSSProperty::kInherited, false));
}

TEST(CascadeFilterTest, SetDoesOverwrite) {
  auto filter = CascadeFilter()
                    .Add(CSSProperty::kVisited, true)
                    .Add(CSSProperty::kInherited, false);
  EXPECT_TRUE(filter.Rejects(CSSProperty::kVisited, true));
  EXPECT_TRUE(filter.Rejects(CSSProperty::kInherited, false));
  filter = filter.Set(CSSProperty::kVisited, false);
  filter = filter.Set(CSSProperty::kInherited, true);
  // Add has no effect if flags are already set:
  EXPECT_TRUE(filter.Rejects(CSSProperty::kVisited, false));
  EXPECT_TRUE(filter.Rejects(CSSProperty::kInherited, true));
}

}  // namespace blink
