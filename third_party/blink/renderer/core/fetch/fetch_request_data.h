// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FETCH_FETCH_REQUEST_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FETCH_FETCH_REQUEST_DATA_H_

#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/fetch_api.mojom-blink-forward.h"
#include "services/network/public/mojom/referrer_policy.mojom-blink-forward.h"
#include "services/network/public/mojom/url_loader_factory.mojom-blink.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink-forward.h"
#include "third_party/blink/public/platform/web_url_request.h"
#include "third_party/blink/renderer/core/fetch/body_stream_buffer.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_load_priority.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/weborigin/referrer.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class ExceptionState;
class FetchHeaderList;
class SecurityOrigin;
class ScriptState;

class CORE_EXPORT FetchRequestData final
    : public GarbageCollected<FetchRequestData> {
 public:
  enum Tainting { kBasicTainting, kCorsTainting, kOpaqueTainting };
  enum class ForServiceWorkerFetchEvent { kFalse, kTrue };

  static FetchRequestData* Create(ScriptState*,
                                  const mojom::blink::FetchAPIRequest&,
                                  ForServiceWorkerFetchEvent);
  FetchRequestData* Clone(ScriptState*, ExceptionState&);
  FetchRequestData* Pass(ScriptState*, ExceptionState&);

  FetchRequestData();
  ~FetchRequestData();

  void SetMethod(AtomicString method) { method_ = method; }
  const AtomicString& Method() const { return method_; }
  void SetURL(const KURL& url) { url_ = url; }
  const KURL& Url() const { return url_; }
  mojom::RequestContextType Context() const { return context_; }
  void SetContext(mojom::RequestContextType context) { context_ = context; }
  network::mojom::RequestDestination Destination() const {
    return destination_;
  }
  void SetDestination(network::mojom::RequestDestination destination) {
    destination_ = destination;
  }
  scoped_refptr<const SecurityOrigin> Origin() const { return origin_; }
  void SetOrigin(scoped_refptr<const SecurityOrigin> origin) {
    origin_ = std::move(origin);
  }
  scoped_refptr<const SecurityOrigin> IsolatedWorldOrigin() const {
    return isolated_world_origin_;
  }
  void SetIsolatedWorldOrigin(
      scoped_refptr<const SecurityOrigin> isolated_world_origin) {
    isolated_world_origin_ = std::move(isolated_world_origin);
  }
  const AtomicString& ReferrerString() const { return referrer_string_; }
  void SetReferrerString(const AtomicString& s) { referrer_string_ = s; }
  network::mojom::ReferrerPolicy GetReferrerPolicy() const {
    return referrer_policy_;
  }
  void SetReferrerPolicy(network::mojom::ReferrerPolicy p) {
    referrer_policy_ = p;
  }
  void SetMode(network::mojom::RequestMode mode) { mode_ = mode; }
  network::mojom::RequestMode Mode() const { return mode_; }
  void SetCredentials(network::mojom::CredentialsMode credentials) {
    credentials_ = credentials;
  }
  network::mojom::CredentialsMode Credentials() const { return credentials_; }
  void SetCacheMode(mojom::FetchCacheMode cache_mode) {
    cache_mode_ = cache_mode;
  }
  mojom::FetchCacheMode CacheMode() const { return cache_mode_; }
  void SetRedirect(network::mojom::RedirectMode redirect) {
    redirect_ = redirect;
  }
  network::mojom::RedirectMode Redirect() const { return redirect_; }
  void SetImportance(mojom::FetchImportanceMode importance) {
    importance_ = importance;
  }
  mojom::FetchImportanceMode Importance() const { return importance_; }
  void SetResponseTainting(Tainting tainting) { response_tainting_ = tainting; }
  Tainting ResponseTainting() const { return response_tainting_; }
  FetchHeaderList* HeaderList() const { return header_list_.Get(); }
  void SetHeaderList(FetchHeaderList* header_list) {
    header_list_ = header_list;
  }
  BodyStreamBuffer* Buffer() const { return buffer_; }
  void SetBuffer(BodyStreamBuffer* buffer) { buffer_ = buffer; }
  String MimeType() const { return mime_type_; }
  void SetMimeType(const String& type) { mime_type_ = type; }
  String Integrity() const { return integrity_; }
  void SetIntegrity(const String& integrity) { integrity_ = integrity; }
  ResourceLoadPriority Priority() const { return priority_; }
  void SetPriority(ResourceLoadPriority priority) { priority_ = priority; }
  bool Keepalive() const { return keepalive_; }
  void SetKeepalive(bool b) { keepalive_ = b; }
  bool IsHistoryNavigation() const { return is_history_navigation_; }
  void SetIsHistoryNavigation(bool b) { is_history_navigation_ = b; }

  network::mojom::blink::URLLoaderFactory* URLLoaderFactory() const {
    return url_loader_factory_.is_bound() ? url_loader_factory_.get() : nullptr;
  }
  void SetURLLoaderFactory(
      mojo::PendingRemote<network::mojom::blink::URLLoaderFactory> factory) {
    url_loader_factory_.Bind(std::move(factory));
  }
  const base::UnguessableToken& WindowId() const { return window_id_; }
  void SetWindowId(const base::UnguessableToken& id) { window_id_ = id; }

  void Trace(Visitor*);

 private:
  FetchRequestData* CloneExceptBody();

  AtomicString method_;
  KURL url_;
  Member<FetchHeaderList> header_list_;
  // FIXME: Support m_skipServiceWorkerFlag;
  mojom::RequestContextType context_;
  network::mojom::RequestDestination destination_;
  scoped_refptr<const SecurityOrigin> origin_;
  scoped_refptr<const SecurityOrigin> isolated_world_origin_;
  // FIXME: Support m_forceOriginHeaderFlag;
  AtomicString referrer_string_;
  network::mojom::ReferrerPolicy referrer_policy_;
  // FIXME: Support m_authenticationFlag;
  // FIXME: Support m_synchronousFlag;
  network::mojom::RequestMode mode_;
  network::mojom::CredentialsMode credentials_;
  // TODO(yiyix): |cache_mode_| is exposed but does not yet affect fetch
  // behavior. We must transfer the mode to the network layer and service
  // worker.
  mojom::FetchCacheMode cache_mode_;
  network::mojom::RedirectMode redirect_;
  mojom::FetchImportanceMode importance_;
  // FIXME: Support m_useURLCredentialsFlag;
  // FIXME: Support m_redirectCount;
  Tainting response_tainting_;
  Member<BodyStreamBuffer> buffer_;
  String mime_type_;
  String integrity_;
  ResourceLoadPriority priority_;
  bool keepalive_;
  bool is_history_navigation_ = false;
  // A specific factory that should be used for this request instead of whatever
  // the system would otherwise decide to use to load this request.
  // Currently used for blob: URLs, to ensure they can still be loaded even if
  // the URL got revoked after creating the request.
  mojo::Remote<network::mojom::blink::URLLoaderFactory> url_loader_factory_;
  base::UnguessableToken window_id_;

  DISALLOW_COPY_AND_ASSIGN(FetchRequestData);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FETCH_FETCH_REQUEST_DATA_H_
