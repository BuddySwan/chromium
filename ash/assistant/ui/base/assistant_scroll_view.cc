// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/assistant/ui/base/assistant_scroll_view.h"

#include <memory>
#include <utility>

#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"

namespace ash {

namespace {

// ContentView ----------------------------------------------------------------

class ContentView : public views::View, views::ViewObserver {
 public:
  ContentView() { AddObserver(this); }

  ~ContentView() override { RemoveObserver(this); }

  // views::View:
  void ChildPreferredSizeChanged(views::View* child) override {
    PreferredSizeChanged();
  }

  void ChildVisibilityChanged(views::View* child) override {
    PreferredSizeChanged();
  }

  // views::ViewObserver:
  void OnChildViewAdded(views::View* view, views::View* child) override {
    PreferredSizeChanged();
  }

  void OnChildViewRemoved(views::View* view, views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ContentView);
};

// InvisibleScrollBar ----------------------------------------------------------

class InvisibleScrollBar : public views::OverlayScrollBar {
 public:
  InvisibleScrollBar(AssistantScrollView* parent, bool horizontal)
      : views::OverlayScrollBar(horizontal), parent_(parent) {}

  ~InvisibleScrollBar() override = default;

  // views::OverlayScrollBar:
  int GetThickness() const override { return 0; }

  void Update(int viewport_size,
              int content_size,
              int content_scroll_offset) override {
    views::OverlayScrollBar::Update(viewport_size, content_size,
                                    content_scroll_offset);

    parent_->OnScrollBarUpdated(this, viewport_size, content_size,
                                content_scroll_offset);
  }

  void VisibilityChanged(views::View* starting_from, bool is_visible) override {
    if (starting_from == this)
      parent_->OnScrollBarVisibilityChanged(this, is_visible);
  }

 private:
  AssistantScrollView* const parent_;  // Owned by view hierarchy, owns |this|.

  DISALLOW_COPY_AND_ASSIGN(InvisibleScrollBar);
};

}  // namespace

// AssistantScrollView ---------------------------------------------------------

AssistantScrollView::AssistantScrollView() {
  InitLayout();
}

AssistantScrollView::~AssistantScrollView() = default;

const char* AssistantScrollView::GetClassName() const {
  return "AssistantScrollView";
}

void AssistantScrollView::OnViewPreferredSizeChanged(views::View* view) {
  OnContentsPreferredSizeChanged(content_view_);
  PreferredSizeChanged();
}

void AssistantScrollView::InitLayout() {
  SetBackgroundColor(SK_ColorTRANSPARENT);
  SetDrawOverflowIndicator(false);

  // Content view.
  auto content_view = std::make_unique<ContentView>();
  content_view->AddObserver(this);
  content_view_ = SetContents(std::move(content_view));

  // Scroll bars.
  horizontal_scroll_bar_ = SetHorizontalScrollBar(
      std::make_unique<InvisibleScrollBar>(this, /*horizontal=*/true));

  vertical_scroll_bar_ = SetVerticalScrollBar(
      std::make_unique<InvisibleScrollBar>(this, /*horizontal=*/false));
}

}  // namespace ash
