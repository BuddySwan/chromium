// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DISPLAY_DISPLAY_CHANGE_DIALOG_H_
#define ASH_DISPLAY_DISPLAY_CHANGE_DIALOG_H_

#include "ash/ash_export.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "base/timer/timer.h"
#include "ui/views/window/dialog_delegate.h"

namespace views {
class Label;
}  // namespace views

namespace ash {

// Modal system dialog that is displayed when a user changes the configuration
// of an external display.
class ASH_EXPORT DisplayChangeDialog : public views::DialogDelegateView {
 public:
  using CancelCallback = base::OnceCallback<void(bool display_was_removed)>;

  DisplayChangeDialog(base::string16 window_title,
                      base::string16 timeout_message_with_placeholder,
                      base::OnceClosure on_accept_callback,
                      CancelCallback on_cancel_callback);
  ~DisplayChangeDialog() override;

  DisplayChangeDialog(const DisplayChangeDialog&) = delete;
  DisplayChangeDialog& operator=(const DisplayChangeDialog&) = delete;

  ui::ModalType GetModalType() const override;
  base::string16 GetWindowTitle() const override;

  // views::View:
  gfx::Size CalculatePreferredSize() const override;

  base::WeakPtr<DisplayChangeDialog> GetWeakPtr();

 private:
  friend class ResolutionNotificationControllerTest;
  FRIEND_TEST_ALL_PREFIXES(ResolutionNotificationControllerTest, Timeout);

  static constexpr uint16_t kDefaultTimeoutInSeconds = 10;

  void OnCancelButtonClicked();

  void OnTimerTick();

  // Returns the string displayed as a message in the dialog which includes a
  // countdown timer.
  base::string16 GetRevertTimeoutString() const;

  // The remaining timeout in seconds.
  uint16_t timeout_count_ = kDefaultTimeoutInSeconds;

  const base::string16 window_title_;
  const base::string16 timeout_message_with_placeholder_;

  views::Label* label_ = nullptr;  // Not owned.
  CancelCallback on_cancel_callback_;

  // The timer to invoke OnTimerTick() every second. This cannot be
  // OneShotTimer since the message contains text "automatically closed in xx
  // seconds..." which has to be updated every second.
  base::RepeatingTimer timer_;

  base::WeakPtrFactory<DisplayChangeDialog> weak_factory_{this};
};

}  // namespace ash

#endif  // ASH_DISPLAY_DISPLAY_CHANGE_DIALOG_H_
