// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/content_verifier/content_verifier_utils.h"

#include "base/strings/string_util.h"

namespace extensions {
namespace content_verifier_utils {

bool TrimDotSpaceSuffix(const base::FilePath::StringType& path,
                        base::FilePath::StringType* out_path) {
  DCHECK(IsDotSpaceFilenameSuffixIgnored())
      << "dot-space suffix shouldn't be trimmed in current system";
  base::FilePath::StringType::size_type trim_pos =
      path.find_last_not_of(FILE_PATH_LITERAL(". "));
  if (trim_pos == base::FilePath::StringType::npos)
    return false;

  *out_path = path.substr(0, trim_pos + 1);
  return true;
}

base::FilePath::StringType CanonicalizeRelativePath(
    const base::FilePath& relative_path) {
  base::FilePath::StringType canonicalized_path =
      relative_path.NormalizePathSeparatorsTo('/').value();
  if (!IsFileAccessCaseSensitive())
    canonicalized_path = base::ToLowerASCII(canonicalized_path);
  if (IsDotSpaceFilenameSuffixIgnored())
    TrimDotSpaceSuffix(canonicalized_path, &canonicalized_path);
  return canonicalized_path;
}

}  // namespace content_verifier_utils
}  // namespace extensions
