// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_BACK_GESTURE_CONTEXTUAL_NUDGE_DELEGATE_H_
#define CHROME_BROWSER_UI_ASH_BACK_GESTURE_CONTEXTUAL_NUDGE_DELEGATE_H_

#include "ash/public/cpp/back_gesture_contextual_nudge_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/aura/window_observer.h"

namespace aura {
class Window;
}

namespace ash {
class BackGestureContextualNudgeController;
}

// BackGestureContextualNudgeDelegate observes |window_|'s active webcontent and
// notify when |window_|'s navigation entry changes.
class BackGestureContextualNudgeDelegate
    : public ash::BackGestureContextualNudgeDelegate,
      public content::WebContentsObserver,
      public TabStripModelObserver,
      public aura::WindowObserver {
 public:
  explicit BackGestureContextualNudgeDelegate(
      ash::BackGestureContextualNudgeController* controller);
  BackGestureContextualNudgeDelegate(
      const BackGestureContextualNudgeDelegate&) = delete;
  BackGestureContextualNudgeDelegate& operator=(
      const BackGestureContextualNudgeDelegate&) = delete;

  ~BackGestureContextualNudgeDelegate() override;

  // ash::BackGestureContextualNudgeDelegate:
  void MaybeStartTrackingNavigation(aura::Window* window) override;

  // content::WebContentsObserver:
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // aura::WindowObserver:
  void OnWindowDestroying(aura::Window* window) override;

 private:
  // Stop tracking the navigation status for |window_|.
  void StopTrackingNavigation();

  aura::Window* window_ = nullptr;  // Current observed window.
  ash::BackGestureContextualNudgeController* const controller_;  // Not owned.
};

#endif  // CHROME_BROWSER_UI_ASH_BACK_GESTURE_CONTEXTUAL_NUDGE_DELEGATE_H_
