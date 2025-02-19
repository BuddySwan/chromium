// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INFOBARS_TEST_FAKE_INFOBAR_DELEGATE_H_
#define IOS_CHROME_BROWSER_INFOBARS_TEST_FAKE_INFOBAR_DELEGATE_H_

#include "components/infobars/core/confirm_infobar_delegate.h"

#include "base/strings/utf_string_conversions.h"

// Fake version of InfoBarDelegate.
class FakeInfobarDelegate : public ConfirmInfoBarDelegate {
 public:
  FakeInfobarDelegate(
      base::string16 message_text = base::ASCIIToUTF16("FakeInfobarDelegate"));
  ~FakeInfobarDelegate() override;

  // Returns InfoBarIdentifier::TEST_INFOBAR.
  InfoBarIdentifier GetIdentifier() const override;

  // Returns the message string to be displayed for the Infobar.
  base::string16 GetMessageText() const override;

 private:
  base::string16 message_text_;
};

#endif  // IOS_CHROME_BROWSER_INFOBARS_TEST_FAKE_INFOBAR_DELEGATE_H_
