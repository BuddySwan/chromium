// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FINDER_H_
#define COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FINDER_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "components/payments/content/web_app_manifest.h"
#include "content/public/browser/payment_app_provider.h"
#include "third_party/blink/public/mojom/payments/payment_request.mojom.h"

class GURL;

template <class T>
class scoped_refptr;

namespace base {
template <typename Type>
struct DefaultSingletonTraits;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

namespace url {
class Origin;
}  // namespace url

namespace payments {

class PaymentManifestDownloader;
class PaymentManifestWebDataService;

// Retrieves service worker payment apps.
class ServiceWorkerPaymentAppFinder {
 public:
  using InstallablePaymentApps =
      std::map<GURL, std::unique_ptr<WebAppInstallationInfo>>;
  using GetAllPaymentAppsCallback =
      base::OnceCallback<void(content::PaymentAppProvider::PaymentApps,
                              InstallablePaymentApps,
                              const std::string& error_message)>;

  static ServiceWorkerPaymentAppFinder* GetInstance();

  // Retrieves all service worker payment apps that can handle payments for
  // |requested_method_data|, verifies these apps are allowed to handle these
  // payment methods, and filters them by their capabilities.
  //
  // |merchant_origin| should be the origin of the iframe that created the
  // PaymentRequest object. It is used by security features like
  // 'Sec-Fetch-Site' and 'Cross-Origin-Resource-Policy'.
  //
  // The payment apps will be returned through |callback|. After |callback| has
  // been invoked, it's safe to show the apps in UI for user to select one of
  // these apps for payment.
  //
  // After |callback| has fired, the factory refreshes its own cache in the
  // background. Once the cache has been refreshed, the factory invokes the
  // |finished_writing_cache_callback_for_testing|.
  //
  // The method should be called on the UI thread.
  void GetAllPaymentApps(
      const url::Origin& merchant_origin,
      content::WebContents* web_contents,
      scoped_refptr<PaymentManifestWebDataService> cache,
      const std::vector<mojom::PaymentMethodDataPtr>& requested_method_data,
      bool may_crawl_for_installable_payment_apps,
      GetAllPaymentAppsCallback callback,
      base::OnceClosure finished_writing_cache_callback_for_testing);

  // Removes |apps| that don't match any of the |requested_method_data| based on
  // the method names and method-specific capabilities.
  static void RemoveAppsWithoutMatchingMethodData(
      const std::vector<mojom::PaymentMethodDataPtr>& requested_method_data,
      content::PaymentAppProvider::PaymentApps* apps);

 private:
  friend struct base::DefaultSingletonTraits<ServiceWorkerPaymentAppFinder>;
  friend class PaymentRequestPaymentAppTest;
  friend class ServiceWorkerPaymentAppFinderBrowserTest;
  friend class HybridRequestSkipUITest;
  friend class PaymentRequestPlatformBrowserTestBase;
  friend class PaymentHandlerJustInTimeInstallationTest;

  ServiceWorkerPaymentAppFinder();
  ~ServiceWorkerPaymentAppFinder();

  // Should be used only in tests.
  // Should be called before every call to GetAllPaymentApps() (because the test
  // downloader is moved into the SelfDeletingServiceWorkerPaymentAppFinder).
  void SetDownloaderAndIgnorePortInOriginComparisonForTesting(
      std::unique_ptr<PaymentManifestDownloader> downloader);

  std::unique_ptr<PaymentManifestDownloader> test_downloader_;

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerPaymentAppFinder);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_SERVICE_WORKER_PAYMENT_APP_FINDER_H_
