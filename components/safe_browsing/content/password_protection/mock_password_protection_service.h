// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_CONTENT_PASSWORD_PROTECTION_MOCK_PASSWORD_PROTECTION_SERVICE_H_
#define COMPONENTS_SAFE_BROWSING_CONTENT_PASSWORD_PROTECTION_MOCK_PASSWORD_PROTECTION_SERVICE_H_

#include "base/macros.h"
#include "components/safe_browsing/content/password_protection/password_protection_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace safe_browsing {

class MockPasswordProtectionService : public PasswordProtectionService {
 public:
  MockPasswordProtectionService();
  MockPasswordProtectionService(
      const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      history::HistoryService* history_service);
  ~MockPasswordProtectionService() override;

  // safe_browsing::PasswordProtectionService
  MOCK_CONST_METHOD0(GetSyncAccountType,
                     safe_browsing::LoginReputationClientRequest::
                         PasswordReuseEvent::SyncAccountType());
  MOCK_CONST_METHOD0(GetBrowserPolicyConnector,
                     const policy::BrowserPolicyConnector*());
  MOCK_CONST_METHOD0(GetCurrentContentAreaSize, gfx::Size());
  MOCK_CONST_METHOD0(GetAccountInfo, AccountInfo());
  MOCK_CONST_METHOD0(IsPrimaryAccountSyncing, bool());
  MOCK_CONST_METHOD0(IsPrimaryAccountSignedIn, bool());
  MOCK_CONST_METHOD0(IsPrimaryAccountGmail, bool());
  MOCK_CONST_METHOD1(GetPasswordProtectionWarningTriggerPref,
                     PasswordProtectionTrigger(ReusedPasswordAccountType));
  MOCK_CONST_METHOD1(GetSignedInNonSyncAccount,
                     AccountInfo(const std::string&));
  MOCK_CONST_METHOD1(IsOtherGaiaAccountGmail, bool(const std::string&));
  MOCK_CONST_METHOD2(IsURLWhitelistedForPasswordEntry,
                     bool(const GURL&, RequestOutcome*));

  MOCK_METHOD0(CanSendSamplePing, bool());
  MOCK_METHOD0(IsExtendedReporting, bool());
  MOCK_METHOD0(IsEnhancedProtection, bool());
  MOCK_METHOD0(IsIncognito, bool());
  MOCK_METHOD0(IsHistorySyncEnabled, bool());
  MOCK_METHOD0(IsUnderAdvancedProtection, bool());
  MOCK_METHOD0(ReportPasswordChanged, void());
  MOCK_METHOD1(UserClickedThroughSBInterstitial, bool(content::WebContents*));
  MOCK_METHOD1(MaybeLogPasswordReuseDetectedEvent, void(content::WebContents*));
  MOCK_METHOD1(SanitizeReferrerChain, void(ReferrerChain*));
  MOCK_METHOD2(ShowInterstitial,
               void(content::WebContents*, ReusedPasswordAccountType));
  MOCK_METHOD2(PersistPhishedSavedPasswordCredential,
               void(const std::string&, const std::vector<std::string>&));
  MOCK_METHOD3(IsPingingEnabled,
               bool(LoginReputationClientRequest::TriggerType,
                    ReusedPasswordAccountType,
                    RequestOutcome*));
  MOCK_METHOD5(ShowModalWarning,
               void(content::WebContents*,
                    RequestOutcome,
                    LoginReputationClientResponse::VerdictType,
                    const std::string&,
                    ReusedPasswordAccountType));
  MOCK_METHOD4(
      MaybeReportPasswordReuseDetected,
      void(content::WebContents*, const std::string&, PasswordType, bool));
  MOCK_METHOD3(UpdateSecurityState,
               void(safe_browsing::SBThreatType,
                    ReusedPasswordAccountType,
                    content::WebContents*));
  MOCK_METHOD2(RemoveUnhandledSyncPasswordReuseOnURLsDeleted,
               void(bool, const history::URLRows&));
  MOCK_METHOD3(FillReferrerChain,
               void(const GURL&,
                    SessionID,
                    LoginReputationClientRequest::Frame*));
  MOCK_METHOD4(MaybeLogPasswordReuseLookupEvent,
               void(content::WebContents*,
                    RequestOutcome,
                    PasswordType,
                    const safe_browsing::LoginReputationClientResponse*));
  MOCK_METHOD3(CanShowInterstitial,
               bool(RequestOutcome, ReusedPasswordAccountType, const GURL&));
  MOCK_METHOD5(MaybeStartPasswordFieldOnFocusRequest,
               void(content::WebContents*,
                    const GURL&,
                    const GURL&,
                    const GURL&,
                    const std::string&));
  MOCK_METHOD6(MaybeStartProtectedPasswordEntryRequest,
               void(content::WebContents*,
                    const GURL&,
                    const std::string&,
                    PasswordType,
                    const std::vector<std::string>&,
                    bool));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockPasswordProtectionService);
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CONTENT_PASSWORD_PROTECTION_MOCK_PASSWORD_PROTECTION_SERVICE_H_
