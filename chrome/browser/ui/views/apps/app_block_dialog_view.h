// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_APPS_APP_BLOCK_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_APPS_APP_BLOCK_DIALOG_VIEW_H_

#include <string>

#include "chrome/browser/ui/views/apps/app_dialog_view.h"

class Profile;

namespace gfx {
class ImageSkia;
}

// The app block dialog that displays the app's name, icon and the block reason.
// It is shown when the app is blocked to notice the user that the app can't be
// launched.
class AppBlockDialogView : public AppDialogView {
 public:
  static AppBlockDialogView* GetActiveViewForTesting();

  AppBlockDialogView(const std::string& app_name,
                     const gfx::ImageSkia& image,
                     Profile* profile);
  ~AppBlockDialogView() override;

  // AppDialogView:
  base::string16 GetWindowTitle() const override;
};

#endif  // CHROME_BROWSER_UI_VIEWS_APPS_APP_BLOCK_DIALOG_VIEW_H_
