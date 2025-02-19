// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_RESOLVE_CONTEXT_H_
#define NET_DNS_RESOLVE_CONTEXT_H_

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/observer_list_types.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_config.h"

namespace net {

class DnsSession;
class HostCache;
class URLRequestContext;

// Per-URLRequestContext data used by HostResolver. Expected to be owned by the
// ContextHostResolver, and all usage/references are expected to be cleaned up
// or cancelled before the URLRequestContext goes out of service.
class NET_EXPORT_PRIVATE ResolveContext
    : public base::CheckedObserver,
      public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  // Number of failures allowed before a DoH server is designated 'unavailable'.
  // In AUTOMATIC mode, non-probe DoH queries should not be sent to DoH servers
  // that have reached this limit.
  //
  // This limit is different from the failure limit that governs insecure async
  // resolver bypass in several ways: the failures need not be consecutive,
  // NXDOMAIN responses are never counted as failures, and the outcome of
  // fallback queries is not taken into account.
  static const int kAutomaticModeFailureLimit = 10;

  ResolveContext(URLRequestContext* url_request_context, bool enable_caching);

  ResolveContext(const ResolveContext&) = delete;
  ResolveContext& operator=(const ResolveContext&) = delete;

  ~ResolveContext() override;

  // TODO(crbug.com/1045507): Rework the server index selection logic and
  // interface to not be susceptible to race conditions on server
  // availability/failure-tracking changing between attempts. As-is, this code
  // can easily result in working servers getting skipped and failing servers
  // getting extra attempts (further inflating the failure tracking).

  // Return the (potentially rotating) index of the first configured server (to
  // be passed to [Doh]ServerIndexToUse()). Always returns 0 if |session| is not
  // the current session.
  size_t FirstServerIndex(bool doh_server, const DnsSession* session);

  // Find the index of a non-DoH server to use for this attempt.  Starts from
  // |starting_server| and finds the first eligible server (wrapping around as
  // necessary) below failure limits, or if no eligible servers are below
  // failure limits, the one with the oldest last failure. If |session| is not
  // the current session, assumes all servers are below failure limits and thus
  // always returns |starting_server|.
  size_t ClassicServerIndexToUse(size_t classic_starting_server_index,
                                 const DnsSession* session);

  // Find the index of a DoH server to use for this attempt. Starts from
  // |starting_doh_server_index| and finds the first eligible server (wrapping
  // around as necessary) below failure limits, or of no eligible servers are
  // below failure limits, the one with the oldest last failure. If in AUTOMATIC
  // mode, a server is only eligible after a successful DoH probe. Returns
  // nullopt if there are no eligible DoH servers or |session| is not the
  // current session.
  base::Optional<size_t> DohServerIndexToUse(
      size_t starting_doh_server_index,
      DnsConfig::SecureDnsMode secure_dns_mode,
      const DnsSession* session);

  // Returns the number of DoH servers with successful probe states. Always 0 if
  // |session| is not the current session.
  size_t NumAvailableDohServers(const DnsSession* session) const;

  // Returns whether |doh_server_index| is marked available. Always |false| if
  // |session| is not the current session.
  bool GetDohServerAvailability(size_t doh_server_index,
                                const DnsSession* session) const;

  // Record the latest DoH probe state. Noop if |session| is not the current
  // session.
  void SetProbeSuccess(size_t doh_server_index,
                       bool success,
                       const DnsSession* session);

  // Record that server failed to respond (due to SRV_FAIL or timeout). If
  // |is_doh_server| and the number of failures has surpassed a threshold,
  // sets the DoH probe state to unavailable. Noop if |session| is not the
  // current session.
  void RecordServerFailure(size_t server_index,
                           bool is_doh_server,
                           const DnsSession* session);

  // Record that server responded successfully. Noop if |session| is not the
  // current session.
  void RecordServerSuccess(size_t server_index,
                           bool is_doh_server,
                           const DnsSession* session);

  // Record how long it took to receive a response from the server. Noop if
  // |session| is not the current session.
  void RecordRtt(size_t server_index,
                 bool is_doh_server,
                 base::TimeDelta rtt,
                 int rv,
                 const DnsSession* session);

  // Return the timeout for the next query. |attempt| counts from 0 and is used
  // for exponential backoff.
  base::TimeDelta NextClassicTimeout(size_t classic_server_index,
                                     int attempt,
                                     const DnsSession* session);

  // Return the timeout for the next DoH query.
  base::TimeDelta NextDohTimeout(size_t doh_server_index,
                                 const DnsSession* session);

  URLRequestContext* url_request_context() { return url_request_context_; }
  void set_url_request_context(URLRequestContext* url_request_context) {
    DCHECK(!url_request_context_);
    DCHECK(url_request_context);
    url_request_context_ = url_request_context;
  }

  HostCache* host_cache() { return host_cache_.get(); }

  // Invalidate or clear saved per-context cached data that is not expected to
  // stay valid between connections or sessions (eg the HostCache and DNS server
  // stats). |new_session|, if non-null, will be the new "current" session for
  // which per-session data will be kept.
  void InvalidateCaches(const DnsSession* new_session);

  const DnsSession* current_session_for_testing() const {
    return current_session_.get();
  }

 private:
  struct ServerStats;

  bool IsCurrentSession(const DnsSession* session) const;

  // Returns the ServerStats for the designated server. Returns nullptr if no
  // ServerStats found.
  ServerStats* GetServerStats(size_t server_index, bool is_doh_server);

  // Return the timeout for the next query.
  base::TimeDelta NextTimeoutHelper(ServerStats* server_stats, int attempt);

  // Record the time to perform a query.
  void RecordRttForUma(size_t server_index,
                       bool is_doh_server,
                       base::TimeDelta rtt,
                       int rv);

  // NetworkChangeNotifier::NetworkChangeObserver:
  void OnNetworkChanged(NetworkChangeNotifier::ConnectionType type) override;

  URLRequestContext* url_request_context_;

  std::unique_ptr<HostCache> host_cache_;

  // Current maximum server timeout. Updated on connection change.
  base::TimeDelta max_timeout_;

  // Per-session data is only stored and valid for the latest session. Before
  // accessing, should check that |current_session_| is valid and matches a
  // passed in DnsSession.
  //
  // Using a WeakPtr, so even if a new session has the same pointer as an old
  // invalidated session, it can be recognized as a different session.
  //
  // TODO(crbug.com/1022059): Make const DnsSession once server stats have been
  // moved and no longer need to be read from DnsSession for availability logic.
  base::WeakPtr<const DnsSession> current_session_;
  std::vector<bool> doh_server_availability_;
  // Current index into |config_.nameservers| to begin resolution with.
  int classic_server_index_ = 0;
  base::TimeDelta initial_timeout_;
  // Track runtime statistics of each classic (insecure) DNS server.
  std::vector<ServerStats> classic_server_stats_;
  // Track runtime statistics of each DoH server.
  std::vector<ServerStats> doh_server_stats_;
};

}  // namespace net

#endif  // NET_DNS_RESOLVE_CONTEXT_H_
