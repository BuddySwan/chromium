// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_MAC_SETUP_SETUP_H_
#define CHROME_UPDATER_MAC_SETUP_SETUP_H_

namespace updater {

int Uninstall(bool is_machine);

namespace setup {

int UpdaterSetupMain(int argc, const char* const* argv);

}  // namespace setup

}  // namespace updater

#endif  // CHROME_UPDATER_MAC_SETUP_SETUP_H_
