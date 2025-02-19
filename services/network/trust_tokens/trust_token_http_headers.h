// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_HTTP_HEADERS_H_
#define SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_HTTP_HEADERS_H_

namespace network {

// These are the HTTP headers defined in the Trust Tokens draft explainer:
// https://github.com/WICG/trust-token-api

// As a request header: during issuance, sends a collection of unsigned, blinded
// tokens; during redemption, sends a single signed, unblinded token
// along with associated redemption metadata.
// As a response header: during issuance, provides a collection of signed,
// blinded tokens; during redemption, includes a just-created Signed Redemption
// Record.
constexpr char kTrustTokensSecTrustTokenHeader[] = "Sec-Trust-Token";

// As a request header, provides a timestamp associated with a
// particular Trust Tokens signature-bearing request.
constexpr char kTrustTokensRequestHeaderSecTime[] = "Sec-Time";

// As a request header, provides a signature over the canonical record
// associated with a given request (containing the request's URL; optionally, a
// collection of headers; and, optionally, the request's body).
constexpr char kTrustTokensRequestHeaderSecSignature[] = "Sec-Signature";

// As a request header, provides a Signed Redemption Record obtained from a
// prior issuance-and-redemption flow.
constexpr char kTrustTokensRequestHeaderSecSignedRedemptionRecord[] =
    "Sec-Signed-Redemption-Record";

}  // namespace network

#endif  // SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_HTTP_HEADERS_H_
