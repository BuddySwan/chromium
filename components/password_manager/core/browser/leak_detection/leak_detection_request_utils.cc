// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/leak_detection/leak_detection_request_utils.h"

#include <string>

#include "base/containers/span.h"
#include "base/debug/dump_without_crashing.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece_forward.h"
#include "base/strings/string_util.h"
#include "base/task/post_task.h"
#include "base/task_runner_util.h"
#include "components/password_manager/core/browser/leak_detection/encryption_utils.h"
#include "components/password_manager/core/browser/leak_detection/single_lookup_response.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "crypto/sha2.h"
#include "google_apis/gaia/core_account_id.h"

namespace password_manager {
namespace {

constexpr char kAPIScope[] =
    "https://www.googleapis.com/auth/identity.passwords.leak.check";

// Returns a Google account that can be used for getting a token.
CoreAccountId GetAccountForRequest(
    const signin::IdentityManager* identity_manager) {
  CoreAccountInfo result = identity_manager->GetPrimaryAccountInfo(
      signin::ConsentLevel::kNotRequired);
  if (result.IsEmpty()) {
    std::vector<CoreAccountInfo> all_accounts =
        identity_manager->GetAccountsWithRefreshTokens();
    if (!all_accounts.empty())
      result = all_accounts.front();
  }
  return result.account_id;
}

// Produce |username_hash_prefix| and scrypt hash of the arguments.
// Scrypt computation is actually long.
LookupSingleLeakPayload ProduceHashes(base::StringPiece username,
                                      base::StringPiece password) {
  std::string canonicalized_username = CanonicalizeUsername(username);
  LookupSingleLeakPayload payload;
  payload.username_hash_prefix = BucketizeUsername(canonicalized_username);
  payload.encrypted_payload =
      ScryptHashUsernameAndPassword(canonicalized_username, password);
  if (payload.encrypted_payload.empty())
    return LookupSingleLeakPayload();
  return payload;
}

// Despite the function is short, it executes long. That's why it should be done
// asynchronously.
LookupSingleLeakData PrepareLookupSingleLeakData(base::StringPiece username,
                                                 base::StringPiece password) {
  LookupSingleLeakData data;
  data.payload = ProduceHashes(username, password);
  if (data.payload.encrypted_payload.empty())
    return LookupSingleLeakData();
  data.payload.encrypted_payload =
      CipherEncrypt(data.payload.encrypted_payload, &data.encryption_key);
  return data.payload.encrypted_payload.empty() ? LookupSingleLeakData()
                                                : std::move(data);
}

// Despite the function is short, it executes long. That's why it should be done
// asynchronously.
LookupSingleLeakPayload PrepareLookupSingleLeakDataWithKey(
    const std::string& encryption_key,
    base::StringPiece username,
    base::StringPiece password) {
  LookupSingleLeakPayload payload = ProduceHashes(username, password);
  if (payload.encrypted_payload.empty())
    return LookupSingleLeakPayload();
  payload.encrypted_payload =
      CipherEncryptWithKey(payload.encrypted_payload, encryption_key);
  return payload.encrypted_payload.empty() ? LookupSingleLeakPayload()
                                           : std::move(payload);
}

// Searches |reencrypted_lookup_hash| in the |encrypted_leak_match_prefixes|
// array. |encryption_key| is the original client key used to encrypt the
// payload.
AnalyzeResponseResult CheckIfCredentialWasLeaked(
    std::unique_ptr<SingleLookupResponse> response,
    const std::string& encryption_key) {
  std::string decrypted_username_password =
      CipherDecrypt(response->reencrypted_lookup_hash, encryption_key);
  if (decrypted_username_password.empty()) {
    DLOG(ERROR) << "Can't decrypt data="
                << base::HexEncode(base::as_bytes(
                       base::make_span(response->reencrypted_lookup_hash)));
    return AnalyzeResponseResult::kDecryptionError;
  }

  std::string hash_username_password =
      crypto::SHA256HashString(decrypted_username_password);

  const ptrdiff_t matched_prefixes =
      std::count_if(response->encrypted_leak_match_prefixes.begin(),
                    response->encrypted_leak_match_prefixes.end(),
                    [&hash_username_password](const std::string& prefix) {
                      return base::StartsWith(hash_username_password, prefix,
                                              base::CompareCase::SENSITIVE);
                    });
  switch (matched_prefixes) {
    case 0:
      return AnalyzeResponseResult::kNotLeaked;
    case 1:
      return AnalyzeResponseResult::kLeaked;
    default:
      // In theory this should never happen, due to the API contract the server
      // provides. Generate a crash dump in case it does, so that we get
      // notified.
      base::debug::DumpWithoutCrashing();
      return AnalyzeResponseResult::kLeaked;
  };
}

}  // namespace

void PrepareSingleLeakRequestData(const std::string& username,
                                  const std::string& password,
                                  SingleLeakRequestDataCallback callback) {
  base::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&PrepareLookupSingleLeakData, username, password),
      std::move(callback));
}

void PrepareSingleLeakRequestData(
    base::TaskRunner* task_runner,
    const std::string& encryption_key,
    const std::string& username,
    const std::string& password,
    base::OnceCallback<void(LookupSingleLeakPayload)> callback) {
  base::PostTaskAndReplyWithResult(
      task_runner, FROM_HERE,
      base::BindOnce(&PrepareLookupSingleLeakDataWithKey, encryption_key,
                     username, password),
      std::move(callback));
}

void AnalyzeResponse(std::unique_ptr<SingleLookupResponse> response,
                     const std::string& encryption_key,
                     SingleLeakResponseAnalysisCallback callback) {
  base::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&CheckIfCredentialWasLeaked, std::move(response),
                     encryption_key),
      std::move(callback));
}

std::unique_ptr<signin::AccessTokenFetcher> RequestAccessToken(
    signin::IdentityManager* identity_manager,
    signin::AccessTokenFetcher::TokenCallback callback) {
  return identity_manager->CreateAccessTokenFetcherForAccount(
      GetAccountForRequest(identity_manager),
      /*consumer_name=*/"leak_detection_service", {kAPIScope},
      std::move(callback), signin::AccessTokenFetcher::Mode::kImmediate);
}

}  // namespace password_manager
