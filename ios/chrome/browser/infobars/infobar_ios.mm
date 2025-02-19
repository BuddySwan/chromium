// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/infobars/infobar_ios.h"

#include "base/logging.h"
#include "ios/chrome/browser/infobars/infobar_controller.h"
#import "ios/chrome/browser/ui/infobars/infobar_ui_delegate.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using infobars::InfoBar;
using infobars::InfoBarDelegate;

InfoBarIOS::InfoBarIOS(id<InfobarUIDelegate> controller,
                       std::unique_ptr<InfoBarDelegate> delegate,
                       bool skip_banner)
    : InfoBar(std::move(delegate)),
      controller_(controller),
      skip_banner_(skip_banner) {
  DCHECK(controller_);
  [controller_ setDelegate:this];
}

InfoBarIOS::~InfoBarIOS() {
  DCHECK(controller_);
  [controller_ detachView];
  controller_ = nil;
  for (auto& observer : observers_) {
    observer.InfobarDestroyed(this);
  }
}

void InfoBarIOS::set_accepted(bool accepted) {
  if (accepted_ == accepted)
    return;
  accepted_ = accepted;
  for (auto& observer : observers_) {
    observer.DidUpdateAcceptedState(this);
  }
}

id<InfobarUIDelegate> InfoBarIOS::InfobarUIDelegate() {
  DCHECK(controller_);
  return controller_;
}

void InfoBarIOS::RemoveView() {
  DCHECK(controller_);
  [controller_ removeView];
}

base::WeakPtr<InfoBarIOS> InfoBarIOS::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

#pragma mark - InfoBarControllerDelegate

bool InfoBarIOS::IsOwned() {
  return owner() != nullptr;
}

void InfoBarIOS::RemoveInfoBar() {
  RemoveSelf();
}
