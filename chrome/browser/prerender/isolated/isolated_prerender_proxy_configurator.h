// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_ISOLATED_ISOLATED_PRERENDER_PROXY_CONFIGURATOR_H_
#define CHROME_BROWSER_PRERENDER_ISOLATED_ISOLATED_PRERENDER_PROXY_CONFIGURATOR_H_

#include "base/sequence_checker.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/mojom/network_context.mojom.h"

// Configures the use of the IP-masking CONNECT tunnel proxy for Isolated
// Prerenders.
class IsolatedPrerenderProxyConfigurator {
 public:
  IsolatedPrerenderProxyConfigurator();
  ~IsolatedPrerenderProxyConfigurator();

  void UpdateTunnelHeaders(
      const net::HttpRequestHeaders& connect_tunnel_headers);

  // Adds a config client that can be used to update Data Reduction Proxy
  // settings.
  void AddCustomProxyConfigClient(
      mojo::Remote<network::mojom::CustomProxyConfigClient> config_client);

  // Should be called whenever there is a possible change to the custom proxy
  // config.
  void UpdateCustomProxyConfig();

 private:
  // Creates a config that can be sent to the NetworkContext.
  network::mojom::CustomProxyConfigPtr CreateCustomProxyConfig() const;

  // The headers used to setup the connect tunnel.
  net::HttpRequestHeaders connect_tunnel_headers_;

  // The set of clients that will get updates about changes to the proxy config.
  mojo::RemoteSet<network::mojom::CustomProxyConfigClient>
      proxy_config_clients_;

  SEQUENCE_CHECKER(sequence_checker_);

  IsolatedPrerenderProxyConfigurator(
      const IsolatedPrerenderProxyConfigurator&) = delete;
  IsolatedPrerenderProxyConfigurator& operator=(
      const IsolatedPrerenderProxyConfigurator&) = delete;
};

#endif  // CHROME_BROWSER_PRERENDER_ISOLATED_ISOLATED_PRERENDER_PROXY_CONFIGURATOR_H_
