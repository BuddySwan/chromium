// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media_error.h"
#include <memory>
#include "media/base/media_serializers.h"

namespace media {

MediaError::MediaError() = default;

MediaError::MediaError(ErrorCode code,
                       base::StringPiece message,
                       const base::Location& location) {
  DCHECK(code != ErrorCode::kOk);
  data_ = std::make_unique<MediaErrorInternal>(code, message.as_string());
  AddFrame(location);
}

// Copy Constructor
MediaError::MediaError(const MediaError& copy) {
  *this = copy;
}

MediaError& MediaError::operator=(const MediaError& copy) {
  if (copy.IsOk()) {
    data_.reset();
    return *this;
  }

  data_ = std::make_unique<MediaErrorInternal>(copy.GetErrorCode(),
                                               copy.GetErrorMessage());
  for (const base::Value& frame : copy.data_->frames)
    data_->frames.push_back(frame.Clone());
  for (const MediaError& err : copy.data_->causes)
    data_->causes.push_back(err);
  data_->data = copy.data_->data.Clone();
  return *this;
}

// Allow move.
MediaError::MediaError(MediaError&&) = default;
MediaError& MediaError::operator=(MediaError&&) = default;

MediaError::~MediaError() = default;

MediaError::MediaErrorInternal::MediaErrorInternal(ErrorCode code,
                                                   std::string message)
    : code(code),
      message(std::move(message)),
      data(base::Value(base::Value::Type::DICTIONARY)) {}

MediaError::MediaErrorInternal::~MediaErrorInternal() = default;

MediaError&& MediaError::AddHere(const base::Location& location) && {
  DCHECK(data_);
  AddFrame(location);
  return std::move(*this);
}

MediaError&& MediaError::AddCause(MediaError&& cause) && {
  DCHECK(data_ && cause.data_);
  data_->causes.push_back(std::move(cause));
  return std::move(*this);
}

void MediaError::AddFrame(const base::Location& location) {
  DCHECK(data_);
  data_->frames.push_back(MediaSerialize(location));
}

}  // namespace media
