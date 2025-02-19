// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/common/omnibox_features.h"

#include "build/build_config.h"

namespace omnibox {

// Allows Omnibox to dynamically adjust number of offered suggestions to fill in
// the space between Omnibox an the soft keyboard. The number of suggestions
// shown will be no less than minimum for the platform (eg. 5 for Android).
const base::Feature kAdaptiveSuggestionsCount{
    "OmniboxAdaptiveSuggestionsCount", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to hide the scheme from steady state URLs displayed in the
// toolbar. It is restored during editing.
const base::Feature kHideFileUrlScheme {
  "OmniboxUIExperimentHideFileUrlScheme",
// Android and iOS don't have the File security chip, and therefore still
// need to show the file scheme.
#if defined(OS_ANDROID) || defined(OS_IOS)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature used to hide the scheme from steady state URLs displayed in the
// toolbar. It is restored during editing.
const base::Feature kHideSteadyStateUrlScheme{
    "OmniboxUIExperimentHideSteadyStateUrlScheme",
    base::FEATURE_ENABLED_BY_DEFAULT};

// Feature used to hide trivial subdomains from steady state URLs displayed in
// the toolbar. It is restored during editing.
const base::Feature kHideSteadyStateUrlTrivialSubdomains{
    "OmniboxUIExperimentHideSteadyStateUrlTrivialSubdomains",
    base::FEATURE_ENABLED_BY_DEFAULT};

// Feature used to hide the path, query and ref from steady state URLs
// displayed in the toolbar. It is restored during editing.
const base::Feature kHideSteadyStateUrlPathQueryAndRef {
  "OmniboxUIExperimentHideSteadyStateUrlPathQueryAndRef",
#if defined(OS_IOS)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Feature used to undo all omnibox elisions on a single click or focus action.
const base::Feature kOneClickUnelide{"OmniboxOneClickUnelide",
                                     base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to enable local entity suggestions. Similar to rich entities but
// but location specific. E.g., typing 'starbucks near' could display the local
// entity suggestion 'starbucks near disneyland \n starbucks * Anaheim, CA'.
const base::Feature kOmniboxLocalEntitySuggestions{
    "OmniboxLocalEntitySuggestions", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to cap the number of URL-type matches shown within the
// Omnibox. If enabled, the number of URL-type matches is limited (unless
// there are no more non-URL matches available.) If enabled, there is a
// companion parameter - OmniboxMaxURLMatches - which specifies the maximum
// desired number of URL-type matches.
const base::Feature kOmniboxMaxURLMatches {
  "OmniboxMaxURLMatches",
#if defined(OS_IOS) || defined(OS_ANDROID)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature used to enable entity suggestion images and enhanced presentation
// showing more context and descriptive text about the entity.
const base::Feature kOmniboxRichEntitySuggestions{
    "OmniboxRichEntitySuggestions",
#if defined(OS_IOS) || defined(OS_ANDROID)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature used to enable swapping the rows on answers.
const base::Feature kOmniboxReverseAnswers{"OmniboxReverseAnswers",
                                           base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to enable matching short words to bookmarks for suggestions.
const base::Feature kOmniboxShortBookmarkSuggestions{
    "OmniboxShortBookmarkSuggestions", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to force on the experiment of transmission of tail suggestions
// from GWS to this client, currently testing for desktop.
const base::Feature kOmniboxTailSuggestions{
    "OmniboxTailSuggestions", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature that enables the tab-switch suggestions corresponding to an open
// tab, for a button or dedicated suggestion. Enabled by default on Desktop
// and iOS.
const base::Feature kOmniboxTabSwitchSuggestions{
  "OmniboxTabSwitchSuggestions",
#if defined(OS_ANDROID)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature that enables tab-switch suggestions in their own row.
const base::Feature kOmniboxTabSwitchSuggestionsDedicatedRow{
    "OmniboxTabSwitchSuggestionsDedicatedRow",
    base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to enable various experiments on keyword mode, UI and
// suggestions.
const base::Feature kExperimentalKeywordMode{"OmniboxExperimentalKeywordMode",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to enable Pedal suggestions.
const base::Feature kOmniboxPedalSuggestions{"OmniboxPedalSuggestions",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used for UI that improves transparency of and control over omnibox
// suggestions. This includes "Why this Suggestion?" and user controls to delete
// personalized suggestions. This will be eventually enabled by default.
const base::Feature kOmniboxSuggestionTransparencyOptions{
    "OmniboxSuggestionTransparencyOptions", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature to enable clipboard provider to suggest copied text.
const base::Feature kEnableClipboardProviderTextSuggestions{
  "OmniboxEnableClipboardProviderTextSuggestions",
#if defined(OS_IOS)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Feature to enable clipboard provider to suggest searching for copied images.
const base::Feature kEnableClipboardProviderImageSuggestions{
  "OmniboxEnableClipboardProviderImageSuggestions",
#if defined(OS_IOS)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Feature to enable the search provider to send a request to the suggest
// server on focus.  This allows the suggest server to warm up, by, for
// example, loading per-user models into memory.  Having a per-user model
// in memory allows the suggest server to respond more quickly with
// personalized suggestions as the user types.
const base::Feature kSearchProviderWarmUpOnFocus{
  "OmniboxWarmUpSearchProviderOnFocus",
#if defined(OS_IOS)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature used to display the title of the current URL match.
const base::Feature kDisplayTitleForCurrentUrl{
  "OmniboxDisplayTitleForCurrentUrl",
#if !defined(OS_IOS)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Feature used for the max autocomplete matches UI experiment.
const base::Feature kUIExperimentMaxAutocompleteMatches{
    "OmniboxUIExperimentMaxAutocompleteMatches",
    base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to display the search terms instead of the URL in the Omnibox
// when the user is on the search results page of the default search provider.
const base::Feature kQueryInOmnibox{"QueryInOmnibox",
                                    base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to always swap the title and URL.
const base::Feature kUIExperimentSwapTitleAndUrl{
    "OmniboxUIExperimentSwapTitleAndUrl",
#if defined(OS_IOS) || defined(OS_ANDROID)
    base::FEATURE_DISABLED_BY_DEFAULT
#else
    base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

// Feature used to enable speculatively starting a service worker associated
// with the destination of the default match when the user's input looks like a
// query.
const base::Feature kSpeculativeServiceWorkerStartOnQueryInput{
  "OmniboxSpeculativeServiceWorkerStartOnQueryInput",
      base::FEATURE_ENABLED_BY_DEFAULT
};

// Feature used to fetch document suggestions.
const base::Feature kDocumentProvider{"OmniboxDocumentProvider",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to autocomplete bookmark, history, and document suggestions when
// the user input is a prefix of their titles, as opposed to their URLs.
const base::Feature kAutocompleteTitles{"OmniboxAutocompleteTitles",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

// Feature to use material design weather icons in the omnibox when displaying
// weather answers.
const base::Feature kOmniboxMaterialDesignWeatherIcons{
    "OmniboxMaterialDesignWeatherIcons", base::FEATURE_ENABLED_BY_DEFAULT};

// Returns whether IsInstantExtendedAPIEnabled should be ignored when deciding
// the number of Google-provided search suggestions.
const base::Feature kOmniboxDisableInstantExtendedLimit{
    "OmniboxDisableInstantExtendedLimit", base::FEATURE_DISABLED_BY_DEFAULT};

// Show the search engine logo in the omnibox on Android (desktop already does
// this).
const base::Feature kOmniboxSearchEngineLogo{"OmniboxSearchEngineLogo",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

// Feature used to allow users to remove suggestions from clipboard.
const base::Feature kOmniboxRemoveSuggestionsFromClipboard{
    "OmniboxRemoveSuggestionsFromClipboard", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature to provide non personalized head search suggestion from a compact
// on device model.
const base::Feature kOnDeviceHeadProvider{"OmniboxOnDeviceHeadProvider",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

// Feature to debounce drive requests from the document provider.
const base::Feature kDebounceDocumentProvider{
    "OmniboxDebounceDocumentProvider", base::FEATURE_DISABLED_BY_DEFAULT};

// Preserves the default match against change when providers return results
// asynchronously. This prevents the default match from changing after the user
// finishes typing. Without this feature, if the default match is updated right
// when the user presses Enter, the user may go to a surprising destination.
const base::Feature kOmniboxPreserveDefaultMatchAgainstAsyncUpdate{
    "OmniboxPreserveDefaultMatchAgainstAsyncUpdate",
    base::FEATURE_DISABLED_BY_DEFAULT};

// Demotes the relevance scores when comparing suggestions based on the
// suggestion's |AutocompleteMatchType| and the user's |PageClassification|.
// This feature's main job is to contain the DemoteByType parameter.
const base::Feature kOmniboxDemoteByType{"OmniboxDemoteByType",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

// Feature to configure on-focus suggestions provided by ZeroSuggestProvider.
// This feature's main job is to contain some field trial parameters such as:
//  - "ZeroSuggestVariant" configures the per-page-classification mode of
//    ZeroSuggestProvider.
const base::Feature kOnFocusSuggestions{"OmniboxOnFocusSuggestions",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

// Allow suggestions to be shown to the user on the New Tab Page upon focusing
// URL bar (the omnibox).
const base::Feature kZeroSuggestionsOnNTP{"OmniboxZeroSuggestionsOnNTP",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

// Allow suggestions to be shown to the user on the New Tab Page upon focusing
// the real search box.
const base::Feature kZeroSuggestionsOnNTPRealbox{
    "OmniboxZeroSuggestionsOnNTPRealbox", base::FEATURE_DISABLED_BY_DEFAULT};

// Allow on-focus query refinements to be shown on the default SERP.
const base::Feature kZeroSuggestionsOnSERP{"OmniboxZeroSuggestionsOnSERP",
                                           base::FEATURE_ENABLED_BY_DEFAULT};

// If enabled, changes the way Google-provided search suggestions are scored by
// the backend. Note that this Feature is only used for triggering a server-
// side experiment config that will send experiment IDs to the backend. It is
// not referred to in any of the Chromium code.
const base::Feature kOmniboxExperimentalSuggestScoring{
    "OmniboxExperimentalSuggestScoring", base::FEATURE_DISABLED_BY_DEFAULT};

// If disabled, terms with no wordstart matches disqualify the suggestion unless
// they occur in the URL host. If enabled, terms with no wordstart matches are
// allowed but not scored. E.g., both inputs 'java script' and 'java cript' will
// match a suggestion titled 'javascript' and score equivalently.
const base::Feature kHistoryQuickProviderAllowButDoNotScoreMidwordTerms{
    "OmniboxHistoryQuickProviderAllowButDoNotScoreMidwordTerms",
    base::FEATURE_DISABLED_BY_DEFAULT};

// If disabled, midword matches are ignored except in the URL host, and input
// terms with no wordstart matches are scored 0, resulting in an overall score
// of 0. If enabled, midword matches are allowed and scored when they begin
// immediately after the previous match ends. E.g. 'java script' will match a
// suggestion titled 'javascript' but the input 'java cript' won't.
const base::Feature kHistoryQuickProviderAllowMidwordContinuations{
    "OmniboxHistoryQuickProviderAllowMidwordContinuations",
    base::FEATURE_DISABLED_BY_DEFAULT};

// If enabled, shows a confirm dialog before removing search suggestions from
// the omnibox. See ConfirmNtpSuggestionRemovals for the NTP equivalent.
const base::Feature kConfirmOmniboxSuggestionRemovals{
    "ConfirmOmniboxSuggestionRemovals", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature that enables not counting submatches towards the maximum
// suggestion limit.
const base::Feature kOmniboxLooseMaxLimitOnDedicatedRows{
    "OmniboxLooseMaxLimitOnDedicatedRows", base::FEATURE_DISABLED_BY_DEFAULT};

// Feature that puts a single row of buttons on suggestions with actionable
// elements like keywords, tab-switch buttons, and Pedals.
const base::Feature kOmniboxSuggestionButtonRow{
    "OmniboxSuggestionButtonRow", base::FEATURE_DISABLED_BY_DEFAULT};

// If enabled, allows Tab and Shift+Tab to escape the focus out of the omnibox
// popup. Otherwise, Tab infinitely loops between suggestions in the popup.
const base::Feature kTabKeyCanEscapeOmniboxPopup{
    "TabKeyCanEscapeOmniboxPopup", base::FEATURE_DISABLED_BY_DEFAULT};

// If enabled, uses WebUI to render the omnibox suggestions popup, similar to
// how the NTP "fakebox" is implemented.
const base::Feature kWebUIOmniboxPopup{"WebUIOmniboxPopup",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

// When enabled, use Assistant for omnibox voice query recognition instead of
// Android's built-in voice recognition service. Only works on Android.
const base::Feature kOmniboxAssistantVoiceSearch{
    "OmniboxAssistantVoiceSearch", base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace omnibox
