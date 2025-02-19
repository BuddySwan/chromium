// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/chrome_signin_proxying_url_loader_factory.h"

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/supports_user_data.h"
#include "build/buildflag.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/chrome_signin_helper.h"
#include "chrome/browser/signin/header_modification_delegate.h"
#include "chrome/browser/signin/header_modification_delegate_impl.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "extensions/browser/guest_view/web_view/web_view_renderer_state.h"
#include "extensions/buildflags/buildflags.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/url_loader.mojom.h"

namespace signin {

namespace {

// User data key for BrowserContextData.
const void* const kBrowserContextUserDataKey = &kBrowserContextUserDataKey;

// Owns all of the ProxyingURLLoaderFactorys for a given Profile.
class BrowserContextData : public base::SupportsUserData::Data {
 public:
  ~BrowserContextData() override {}

  static void StartProxying(
      Profile* profile,
      content::WebContents::Getter web_contents_getter,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory) {
    auto* self = static_cast<BrowserContextData*>(
        profile->GetUserData(kBrowserContextUserDataKey));
    if (!self) {
      self = new BrowserContextData();
      profile->SetUserData(kBrowserContextUserDataKey, base::WrapUnique(self));
    }

    auto delegate = std::make_unique<HeaderModificationDelegateImpl>(profile);
    auto proxy = std::make_unique<ProxyingURLLoaderFactory>(
        std::move(delegate), std::move(web_contents_getter),
        std::move(receiver), std::move(target_factory),
        base::BindOnce(&BrowserContextData::RemoveProxy,
                       self->weak_factory_.GetWeakPtr()));
    self->proxies_.emplace(std::move(proxy));
  }

  void RemoveProxy(ProxyingURLLoaderFactory* proxy) {
    auto it = proxies_.find(proxy);
    DCHECK(it != proxies_.end());
    proxies_.erase(it);
  }

 private:
  BrowserContextData() {}

  std::set<std::unique_ptr<ProxyingURLLoaderFactory>, base::UniquePtrComparator>
      proxies_;

  base::WeakPtrFactory<BrowserContextData> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(BrowserContextData);
};

}  // namespace

class ProxyingURLLoaderFactory::InProgressRequest
    : public network::mojom::URLLoader,
      public network::mojom::URLLoaderClient,
      public base::SupportsUserData {
 public:
  InProgressRequest(
      ProxyingURLLoaderFactory* factory,
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t routing_id,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation);

  ~InProgressRequest() override {
    if (destruction_callback_)
      std::move(destruction_callback_).Run();
  }

  // network::mojom::URLLoader:
  void FollowRedirect(const std::vector<std::string>& removed_headers,
                      const net::HttpRequestHeaders& modified_headers,
                      const base::Optional<GURL>& new_url) override;

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    target_loader_->SetPriority(priority, intra_priority_value);
  }

  void PauseReadingBodyFromNet() override {
    target_loader_->PauseReadingBodyFromNet();
  }

  void ResumeReadingBodyFromNet() override {
    target_loader_->ResumeReadingBodyFromNet();
  }

  // network::mojom::URLLoaderClient:
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr head) override;
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override;

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    target_client_->OnUploadProgress(current_position, total_size,
                                     std::move(callback));
  }

  void OnReceiveCachedMetadata(mojo_base::BigBuffer data) override {
    target_client_->OnReceiveCachedMetadata(std::move(data));
  }

  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    target_client_->OnTransferSizeUpdated(transfer_size_diff);
  }

  void OnStartLoadingResponseBody(
      mojo::ScopedDataPipeConsumerHandle body) override {
    target_client_->OnStartLoadingResponseBody(std::move(body));
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    target_client_->OnComplete(status);
  }

 private:
  class ProxyRequestAdapter;
  class ProxyResponseAdapter;

  void OnBindingsClosed() {
    // Destroys |this|.
    factory_->RemoveRequest(this);
  }

  // Back pointer to the factory which owns this class.
  ProxyingURLLoaderFactory* const factory_;

  // Information about the current request.
  GURL request_url_;
  GURL response_url_;
  GURL referrer_origin_;
  net::HttpRequestHeaders headers_;
  net::RedirectInfo redirect_info_;
  const blink::mojom::ResourceType resource_type_;
  const bool is_main_frame_;

  base::OnceClosure destruction_callback_;

  // Messages received by |client_receiver_| are forwarded to |target_client_|.
  mojo::Receiver<network::mojom::URLLoaderClient> client_receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> target_client_;

  // Messages received by |loader_receiver_| are forwarded to |target_loader_|.
  mojo::Receiver<network::mojom::URLLoader> loader_receiver_;
  mojo::Remote<network::mojom::URLLoader> target_loader_;

  DISALLOW_COPY_AND_ASSIGN(InProgressRequest);
};

class ProxyingURLLoaderFactory::InProgressRequest::ProxyRequestAdapter
    : public ChromeRequestAdapter {
 public:
  ProxyRequestAdapter(InProgressRequest* in_progress_request,
                      const net::HttpRequestHeaders& original_headers,
                      net::HttpRequestHeaders* modified_headers,
                      std::vector<std::string>* removed_headers)
      : in_progress_request_(in_progress_request),
        original_headers_(original_headers),
        modified_headers_(modified_headers),
        removed_headers_(removed_headers) {
    DCHECK(in_progress_request_);
    DCHECK(modified_headers_);
    DCHECK(removed_headers_);
  }

  ~ProxyRequestAdapter() override = default;

  content::WebContents::Getter GetWebContentsGetter() const override {
    return in_progress_request_->factory_->web_contents_getter_;
  }

  blink::mojom::ResourceType GetResourceType() const override {
    return in_progress_request_->resource_type_;
  }

  GURL GetReferrerOrigin() const override {
    return in_progress_request_->referrer_origin_;
  }

  void SetDestructionCallback(base::OnceClosure closure) override {
    if (!in_progress_request_->destruction_callback_)
      in_progress_request_->destruction_callback_ = std::move(closure);
  }

  // signin::RequestAdapter
  const GURL& GetUrl() override { return in_progress_request_->request_url_; }

  bool HasHeader(const std::string& name) override {
    return (original_headers_.HasHeader(name) ||
            modified_headers_->HasHeader(name)) &&
           !base::Contains(*removed_headers_, name);
  }

  void RemoveRequestHeaderByName(const std::string& name) override {
    if (!base::Contains(*removed_headers_, name))
      removed_headers_->push_back(name);
  }

  void SetExtraHeaderByName(const std::string& name,
                            const std::string& value) override {
    modified_headers_->SetHeader(name, value);

    auto it =
        std::find(removed_headers_->begin(), removed_headers_->end(), name);
    if (it != removed_headers_->end())
      removed_headers_->erase(it);
  }

 private:
  InProgressRequest* const in_progress_request_;
  const net::HttpRequestHeaders& original_headers_;
  net::HttpRequestHeaders* modified_headers_;
  std::vector<std::string>* removed_headers_;

  DISALLOW_COPY_AND_ASSIGN(ProxyRequestAdapter);
};

class ProxyingURLLoaderFactory::InProgressRequest::ProxyResponseAdapter
    : public ResponseAdapter {
 public:
  ProxyResponseAdapter(InProgressRequest* in_progress_request,
                       net::HttpResponseHeaders* headers)
      : in_progress_request_(in_progress_request), headers_(headers) {
    DCHECK(in_progress_request_);
    DCHECK(headers_);
  }

  ~ProxyResponseAdapter() override = default;

  // signin::ResponseAdapter
  content::WebContents::Getter GetWebContentsGetter() const override {
    return in_progress_request_->factory_->web_contents_getter_;
  }

  bool IsMainFrame() const override {
    return in_progress_request_->is_main_frame_;
  }

  GURL GetOrigin() const override {
    return in_progress_request_->response_url_.GetOrigin();
  }

  const net::HttpResponseHeaders* GetHeaders() const override {
    return headers_;
  }

  void RemoveHeader(const std::string& name) override {
    headers_->RemoveHeader(name);
  }

  base::SupportsUserData::Data* GetUserData(const void* key) const override {
    return in_progress_request_->GetUserData(key);
  }

  void SetUserData(
      const void* key,
      std::unique_ptr<base::SupportsUserData::Data> data) override {
    in_progress_request_->SetUserData(key, std::move(data));
  }

 private:
  InProgressRequest* const in_progress_request_;
  net::HttpResponseHeaders* const headers_;

  DISALLOW_COPY_AND_ASSIGN(ProxyResponseAdapter);
};

ProxyingURLLoaderFactory::InProgressRequest::InProgressRequest(
    ProxyingURLLoaderFactory* factory,
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t routing_id,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
    : factory_(factory),
      request_url_(request.url),
      response_url_(request.url),
      referrer_origin_(request.referrer.GetOrigin()),
      resource_type_(
          static_cast<blink::mojom::ResourceType>(request.resource_type)),
      is_main_frame_(request.is_main_frame),
      target_client_(std::move(client)),
      loader_receiver_(this, std::move(loader_receiver)) {
  mojo::PendingRemote<network::mojom::URLLoaderClient> proxy_client =
      client_receiver_.BindNewPipeAndPassRemote();

  net::HttpRequestHeaders modified_headers;
  std::vector<std::string> removed_headers;
  ProxyRequestAdapter adapter(this, request.headers, &modified_headers,
                              &removed_headers);
  factory_->delegate_->ProcessRequest(&adapter, GURL() /* redirect_url */);

  if (modified_headers.IsEmpty() && removed_headers.empty()) {
    factory_->target_factory_->CreateLoaderAndStart(
        target_loader_.BindNewPipeAndPassReceiver(), routing_id, request_id,
        options, request, std::move(proxy_client), traffic_annotation);

    // We need to keep a full copy of the request headers in case there is a
    // redirect and the request headers need to be modified again.
    headers_.CopyFrom(request.headers);
  } else {
    network::ResourceRequest request_copy = request;
    request_copy.headers.MergeFrom(modified_headers);
    for (const std::string& name : removed_headers)
      request_copy.headers.RemoveHeader(name);

    factory_->target_factory_->CreateLoaderAndStart(
        target_loader_.BindNewPipeAndPassReceiver(), routing_id, request_id,
        options, request_copy, std::move(proxy_client), traffic_annotation);

    headers_.Swap(&request_copy.headers);
  }

  base::RepeatingClosure closure = base::BarrierClosure(
      2, base::BindOnce(&InProgressRequest::OnBindingsClosed,
                        base::Unretained(this)));
  loader_receiver_.set_disconnect_handler(closure);
  client_receiver_.set_disconnect_handler(closure);
}

void ProxyingURLLoaderFactory::InProgressRequest::FollowRedirect(
    const std::vector<std::string>& removed_headers_ext,
    const net::HttpRequestHeaders& modified_headers_ext,
    const base::Optional<GURL>& opt_new_url) {
  std::vector<std::string> removed_headers = removed_headers_ext;
  net::HttpRequestHeaders modified_headers = modified_headers_ext;
  ProxyRequestAdapter adapter(this, headers_, &modified_headers,
                              &removed_headers);
  factory_->delegate_->ProcessRequest(&adapter, redirect_info_.new_url);

  headers_.MergeFrom(modified_headers);
  for (const std::string& name : removed_headers)
    headers_.RemoveHeader(name);

  target_loader_->FollowRedirect(removed_headers, modified_headers,
                                 opt_new_url);

  request_url_ = redirect_info_.new_url;
  referrer_origin_ = GURL(redirect_info_.new_referrer).GetOrigin();
}

void ProxyingURLLoaderFactory::InProgressRequest::OnReceiveResponse(
    network::mojom::URLResponseHeadPtr head) {
  // Even though |head| is const we can get a non-const pointer to the headers
  // and modifications we made are passed to the target client.
  ProxyResponseAdapter adapter(this, head->headers.get());
  factory_->delegate_->ProcessResponse(&adapter, GURL() /* redirect_url */);
  target_client_->OnReceiveResponse(std::move(head));
}

void ProxyingURLLoaderFactory::InProgressRequest::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    network::mojom::URLResponseHeadPtr head) {
  // Even though |head| is const we can get a non-const pointer to the headers
  // and modifications we made are passed to the target client.
  ProxyResponseAdapter adapter(this, head->headers.get());
  factory_->delegate_->ProcessResponse(&adapter, redirect_info.new_url);
  target_client_->OnReceiveRedirect(redirect_info, std::move(head));

  // The request URL returned by ProxyResponseAdapter::GetOrigin() is updated
  // immediately but the URL and referrer
  redirect_info_ = redirect_info;
  response_url_ = redirect_info.new_url;
}

ProxyingURLLoaderFactory::ProxyingURLLoaderFactory(
    std::unique_ptr<HeaderModificationDelegate> delegate,
    content::WebContents::Getter web_contents_getter,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory,
    DisconnectCallback on_disconnect) {
  DCHECK(proxy_receivers_.empty());
  DCHECK(!target_factory_.is_bound());
  DCHECK(!delegate_);
  DCHECK(!web_contents_getter_);
  DCHECK(!on_disconnect_);

  delegate_ = std::move(delegate);
  web_contents_getter_ = std::move(web_contents_getter);
  on_disconnect_ = std::move(on_disconnect);

  target_factory_.Bind(std::move(target_factory));
  target_factory_.set_disconnect_handler(base::BindOnce(
      &ProxyingURLLoaderFactory::OnTargetFactoryError, base::Unretained(this)));

  proxy_receivers_.Add(this, std::move(loader_receiver));
  proxy_receivers_.set_disconnect_handler(base::BindRepeating(
      &ProxyingURLLoaderFactory::OnProxyBindingError, base::Unretained(this)));
}

ProxyingURLLoaderFactory::~ProxyingURLLoaderFactory() = default;

// static
bool ProxyingURLLoaderFactory::MaybeProxyRequest(
    content::RenderFrameHost* render_frame_host,
    bool is_navigation,
    const url::Origin& request_initiator,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory>* factory_receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // Navigation requests are handled using signin::URLLoaderThrottle.
  if (is_navigation)
    return false;

  if (!render_frame_host)
    return false;

  // This proxy should only be installed for subresource requests from a frame
  // that is rendering the GAIA signon realm.
  if (!gaia::IsGaiaSignonRealm(request_initiator.GetURL()))
    return false;

  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (profile->IsOffTheRecord())
    return false;

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Most requests from guest web views are ignored.
  if (HeaderModificationDelegateImpl::ShouldIgnoreGuestWebViewRequest(
          web_contents)) {
    return false;
  }
#endif

  auto proxied_receiver = std::move(*factory_receiver);
  // TODO(crbug.com/955171): Replace this with PendingRemote.
  mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory_remote;
  *factory_receiver = target_factory_remote.InitWithNewPipeAndPassReceiver();

  auto web_contents_getter =
      base::BindRepeating(&content::WebContents::FromFrameTreeNodeId,
                          render_frame_host->GetFrameTreeNodeId());

  BrowserContextData::StartProxying(profile, std::move(web_contents_getter),
                                    std::move(proxied_receiver),
                                    std::move(target_factory_remote));
  return true;
}

void ProxyingURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t routing_id,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  requests_.insert(std::make_unique<InProgressRequest>(
      this, std::move(loader_receiver), routing_id, request_id, options,
      request, std::move(client), traffic_annotation));
}

void ProxyingURLLoaderFactory::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver) {
  proxy_receivers_.Add(this, std::move(loader_receiver));
}

void ProxyingURLLoaderFactory::OnTargetFactoryError() {
  // Stop calls to CreateLoaderAndStart() when |target_factory_| is invalid.
  target_factory_.reset();
  proxy_receivers_.Clear();

  MaybeDestroySelf();
}

void ProxyingURLLoaderFactory::OnProxyBindingError() {
  if (proxy_receivers_.empty())
    target_factory_.reset();

  MaybeDestroySelf();
}

void ProxyingURLLoaderFactory::RemoveRequest(InProgressRequest* request) {
  auto it = requests_.find(request);
  DCHECK(it != requests_.end());
  requests_.erase(it);

  MaybeDestroySelf();
}

void ProxyingURLLoaderFactory::MaybeDestroySelf() {
  // Even if all URLLoaderFactory pipes connected to this object have been
  // closed it has to stay alive until all active requests have completed.
  if (target_factory_.is_bound() || !requests_.empty())
    return;

  // Deletes |this|.
  std::move(on_disconnect_).Run(this);
}

}  // namespace signin
