/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * (C) 2002-2003 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2002, 2005, 2006, 2008, 2009, 2010, 2012 Apple Inc. All rights
 * reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "third_party/blink/renderer/core/css/style_rule_import.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/loader/resource/css_style_sheet_resource.h"
#include "third_party/blink/renderer/platform/heap/heap.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_initiator_type_names.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_parameters.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_loader_options.h"

namespace blink {

StyleRuleImport::StyleRuleImport(const String& href,
                                 scoped_refptr<MediaQuerySet> media,
                                 OriginClean origin_clean)
    : StyleRuleBase(kImport),
      parent_style_sheet_(nullptr),
      style_sheet_client_(MakeGarbageCollected<ImportedStyleSheetClient>(this)),
      str_href_(href),
      media_queries_(media),
      loading_(false),
      origin_clean_(origin_clean) {
  if (!media_queries_)
    media_queries_ = MediaQuerySet::Create(String(), nullptr);
}

StyleRuleImport::~StyleRuleImport() = default;

void StyleRuleImport::Dispose() {
  style_sheet_client_->Dispose();
}

void StyleRuleImport::TraceAfterDispatch(blink::Visitor* visitor) {
  visitor->Trace(style_sheet_client_);
  visitor->Trace(parent_style_sheet_);
  visitor->Trace(style_sheet_);
  StyleRuleBase::TraceAfterDispatch(visitor);
}

void StyleRuleImport::NotifyFinished(Resource* resource) {
  if (style_sheet_)
    style_sheet_->ClearOwnerRule();

  CSSStyleSheetResource* cached_style_sheet = ToCSSStyleSheetResource(resource);
  Document* document = nullptr;

  // Fallback to an insecure context parser if we don't have a parent style
  // sheet.
  const CSSParserContext* context =
      StrictCSSParserContext(SecureContextMode::kInsecureContext);

  if (parent_style_sheet_) {
    document = parent_style_sheet_->SingleOwnerDocument();
    context = parent_style_sheet_->ParserContext();
  }
  context = MakeGarbageCollected<CSSParserContext>(
      context, cached_style_sheet->GetResponse().ResponseUrl(),
      cached_style_sheet->GetResponse().IsCorsSameOrigin(),
      cached_style_sheet->GetReferrerPolicy(), cached_style_sheet->Encoding(),
      document);

  style_sheet_ = MakeGarbageCollected<StyleSheetContents>(
      context, cached_style_sheet->Url(), this);

  style_sheet_->ParseAuthorStyleSheet(
      cached_style_sheet, document ? document->GetSecurityOrigin() : nullptr);

  loading_ = false;

  if (parent_style_sheet_) {
    parent_style_sheet_->NotifyLoadedSheet(cached_style_sheet);
    parent_style_sheet_->CheckLoaded();
  }
}

bool StyleRuleImport::IsLoading() const {
  return loading_ || (style_sheet_ && style_sheet_->IsLoading());
}

void StyleRuleImport::RequestStyleSheet() {
  if (!parent_style_sheet_)
    return;
  Document* document = parent_style_sheet_->SingleOwnerDocument();
  if (!document)
    return;

  Document* document_for_origin = document;
  if (base::FeatureList::IsEnabled(
          features::kHtmlImportsRequestInitiatorLock) &&
      document->ImportsController()) {
    // For @imports from HTML imported Documents, we use the
    // context document for getting origin and ResourceFetcher to use the main
    // Document's origin, while using the element document for CompleteURL() to
    // use imported Documents' base URLs.
    document_for_origin = document->ContextDocument();
  }

  if (!document_for_origin)
    return;

  ResourceFetcher* fetcher = document_for_origin->Fetcher();
  if (!fetcher)
    return;

  KURL abs_url;
  if (!parent_style_sheet_->BaseURL().IsNull()) {
    // use parent styleheet's URL as the base URL
    abs_url = KURL(parent_style_sheet_->BaseURL(), str_href_);
  } else {
    abs_url = document->CompleteURL(str_href_);
  }

  // Check for a cycle in our import chain.  If we encounter a stylesheet
  // in our parent chain with the same URL, then just bail.
  StyleSheetContents* root_sheet = parent_style_sheet_;
  for (StyleSheetContents* sheet = parent_style_sheet_; sheet;
       sheet = sheet->ParentStyleSheet()) {
    if (EqualIgnoringFragmentIdentifier(abs_url, sheet->BaseURL()) ||
        EqualIgnoringFragmentIdentifier(
            abs_url, document->CompleteURL(sheet->OriginalURL())))
      return;
    root_sheet = sheet;
  }

  ResourceLoaderOptions options;
  options.initiator_info.name = fetch_initiator_type_names::kCSS;
  FetchParameters params(ResourceRequest(abs_url), options);
  params.SetCharset(parent_style_sheet_->Charset());
  params.SetFromOriginDirtyStyleSheet(origin_clean_ != OriginClean::kTrue);
  loading_ = true;
  DCHECK(!style_sheet_client_->GetResource());
  CSSStyleSheetResource::Fetch(params, fetcher, style_sheet_client_);
  if (loading_) {
    // if the import rule is issued dynamically, the sheet may be
    // removed from the pending sheet count, so let the doc know
    // the sheet being imported is pending.
    if (parent_style_sheet_ && parent_style_sheet_->LoadCompleted() &&
        root_sheet == parent_style_sheet_) {
      parent_style_sheet_->StartLoadingDynamicSheet();
    }
  }
}

}  // namespace blink
