// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/request_sender.h"

#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/client_update_protocol/ecdsa.h"
#include "components/update_client/configurator.h"
#include "components/update_client/network.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/utils.h"

namespace update_client {

namespace {

// This is an ECDSA prime256v1 named-curve key.
constexpr int kKeyVersion = 9;
const char kKeyPubBytesBase64[] =
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEsVwVMmIJaWBjktSx9m1JrZWYBvMm"
    "bsrGGQPhScDtao+DloD871YmEeunAaQvRMZgDh1nCaWkVG6wo75+yDbKDA==";

}  // namespace

RequestSender::RequestSender(scoped_refptr<Configurator> config)
    : config_(config) {}

RequestSender::~RequestSender() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void RequestSender::Send(
    const std::vector<GURL>& urls,
    const base::flat_map<std::string, std::string>& request_extra_headers,
    const std::string& request_body,
    bool use_signing,
    RequestSenderCallback request_sender_callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  urls_ = urls;
  request_extra_headers_ = request_extra_headers;
  request_body_ = request_body;
  use_signing_ = use_signing;
  request_sender_callback_ = std::move(request_sender_callback);

  if (urls_.empty()) {
    return HandleSendError(static_cast<int>(ProtocolError::MISSING_URLS), 0);
  }

  cur_url_ = urls_.begin();

  if (use_signing_) {
    public_key_ = GetKey(kKeyPubBytesBase64);
    if (public_key_.empty())
      return HandleSendError(
          static_cast<int>(ProtocolError::MISSING_PUBLIC_KEY), 0);
  }

  SendInternal();
}

void RequestSender::SendInternal() {
  DCHECK(cur_url_ != urls_.end());
  DCHECK(cur_url_->is_valid());
  DCHECK(thread_checker_.CalledOnValidThread());

  GURL url(*cur_url_);

  if (use_signing_) {
    DCHECK(!public_key_.empty());
    signer_ = client_update_protocol::Ecdsa::Create(kKeyVersion, public_key_);
    std::string request_query_string;
    signer_->SignRequest(request_body_, &request_query_string);

    url = BuildUpdateUrl(url, request_query_string);
  }

  VLOG(2) << "Sending Omaha request: " << request_body_;

  network_fetcher_ = config_->GetNetworkFetcherFactory()->Create();
  if (!network_fetcher_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&RequestSender::SendInternalComplete,
                       base::Unretained(this),
                       static_cast<int>(ProtocolError::URL_FETCHER_FAILED),
                       std::string(), std::string(), 0));
  }
  network_fetcher_->PostRequest(
      url, request_body_, request_extra_headers_,
      base::BindOnce(&RequestSender::OnResponseStarted, base::Unretained(this)),
      base::BindRepeating([](int64_t current) {}),
      base::BindOnce(&RequestSender::OnNetworkFetcherComplete,
                     base::Unretained(this), url));
}

void RequestSender::SendInternalComplete(int error,
                                         const std::string& response_body,
                                         const std::string& response_etag,
                                         int retry_after_sec) {
  VLOG(2) << "Omaha response received: " << response_body;

  if (!error) {
    if (!use_signing_) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(request_sender_callback_), 0,
                                    response_body, retry_after_sec));
      return;
    }

    DCHECK(use_signing_);
    DCHECK(signer_);
    if (signer_->ValidateResponse(response_body, response_etag)) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(request_sender_callback_), 0,
                                    response_body, retry_after_sec));
      return;
    }

    error = static_cast<int>(ProtocolError::RESPONSE_NOT_TRUSTED);
  }

  DCHECK(error);

  // A positive |retry_after_sec| is a hint from the server that the client
  // should not send further request until the cooldown has expired.
  if (retry_after_sec <= 0 && ++cur_url_ != urls_.end() &&
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&RequestSender::SendInternal,
                                    base::Unretained(this)))) {
    return;
  }

  HandleSendError(error, retry_after_sec);
}

void RequestSender::OnResponseStarted(int response_code,
                                      int64_t content_length) {
  response_code_ = response_code;
}

void RequestSender::OnNetworkFetcherComplete(
    const GURL& original_url,
    std::unique_ptr<std::string> response_body,
    int net_error,
    const std::string& header_etag,
    int64_t xheader_retry_after_sec) {
  DCHECK(thread_checker_.CalledOnValidThread());

  VLOG(1) << "Request completed from url: " << original_url.spec();

  int error = -1;
  if (!net_error && response_code_ == 200)
    error = 0;
  else if (response_code_ != -1)
    error = response_code_;
  else
    error = net_error;

  int retry_after_sec = -1;
  if (original_url.SchemeIsCryptographic() && error > 0)
    retry_after_sec = base::saturated_cast<int>(xheader_retry_after_sec);

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&RequestSender::SendInternalComplete,
                                base::Unretained(this), error,
                                response_body ? *response_body : std::string(),
                                header_etag, retry_after_sec));
}

void RequestSender::HandleSendError(int error, int retry_after_sec) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(request_sender_callback_), error,
                                std::string(), retry_after_sec));
}

std::string RequestSender::GetKey(const char* key_bytes_base64) {
  std::string result;
  return base::Base64Decode(std::string(key_bytes_base64), &result)
             ? result
             : std::string();
}

GURL RequestSender::BuildUpdateUrl(const GURL& url,
                                   const std::string& query_params) {
  const std::string query_string(
      url.has_query() ? base::StringPrintf("%s&%s", url.query().c_str(),
                                           query_params.c_str())
                      : query_params);
  GURL::Replacements replacements;
  replacements.SetQueryStr(query_string);

  return url.ReplaceComponents(replacements);
}

}  // namespace update_client
