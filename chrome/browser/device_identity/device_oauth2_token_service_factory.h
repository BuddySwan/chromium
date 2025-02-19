// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVICE_IDENTITY_DEVICE_OAUTH2_TOKEN_SERVICE_FACTORY_H_
#define CHROME_BROWSER_DEVICE_IDENTITY_DEVICE_OAUTH2_TOKEN_SERVICE_FACTORY_H_

#include <queue>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

namespace chromeos {

class DeviceOAuth2TokenService;

class DeviceOAuth2TokenServiceFactory {
 public:
  // Returns the instance of the DeviceOAuth2TokenService singleton.  May return
  // nullptr during browser startup and shutdown.  When calling Get(), either
  // make sure that your code executes after browser startup and before shutdown
  // or be careful to call Get() every time (instead of holding a pointer) and
  // check for nullptr to handle cases where you might access
  // DeviceOAuth2TokenService during startup or shutdown.
  static DeviceOAuth2TokenService* Get();

  // Called by ChromeBrowserMainPartsChromeOS in order to bootstrap the
  // DeviceOAuth2TokenService instance after the required global data is
  // available (local state, url loader and CrosSettings).
  static void Initialize(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      PrefService* local_state);

  // Called by ChromeBrowserMainPartsChromeOS in order to shutdown the
  // DeviceOAuth2TokenService instance and cancel all in-flight requests before
  // the required global data is destroyed (local state, request context getter
  // and CrosSettings).
  static void Shutdown();

 private:
  DeviceOAuth2TokenServiceFactory();
  ~DeviceOAuth2TokenServiceFactory();

  DISALLOW_COPY_AND_ASSIGN(DeviceOAuth2TokenServiceFactory);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_DEVICE_IDENTITY_DEVICE_OAUTH2_TOKEN_SERVICE_FACTORY_H_
