// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/renderers/win/media_foundation_source_wrapper.h"

#include <mferror.h>

#include "media/base/audio_decoder_config.h"
#include "media/base/demuxer_stream.h"
#include "media/base/video_decoder_config.h"
#include "media/base/win/mf_helpers.h"

namespace media {

using Microsoft::WRL::ComPtr;

MediaFoundationSourceWrapper::MediaFoundationSourceWrapper() = default;
MediaFoundationSourceWrapper::~MediaFoundationSourceWrapper() {
  if (!cdm_proxy_)
    return;

  // Notify |cdm_proxy_| of last Key IDs.
  std::vector<GUID> key_ids(StreamCount());
  for (uint32_t stream_id = 0; stream_id < StreamCount(); stream_id++) {
    key_ids[stream_id] = media_streams_[stream_id]->GetLastKeyId();
  }

  HRESULT hr = cdm_proxy_->SetLastKeyIds(playback_element_id_, key_ids.data(),
                                         key_ids.size());
  DLOG_IF(ERROR, FAILED(hr))
      << "Failed to notify CDM proxy of last Key IDs. hr=" << hr;
}

HRESULT MediaFoundationSourceWrapper::RuntimeClassInitialize(
    uint64_t playback_element_id,
    MediaResource* media_resource,
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  DVLOG(1) << __func__ << ": this=" << this;

  if (media_resource->GetType() != MediaResource::Type::STREAM) {
    DLOG(ERROR) << "MediaResource is not of Type STREAM";
    return E_INVALIDARG;
  }

  task_runner_ = task_runner;

  auto demuxer_streams = media_resource->GetAllStreams();

  int stream_id = 0;
  for (DemuxerStream* demuxer_stream : demuxer_streams) {
    ComPtr<MediaFoundationStreamWrapper> mf_stream;
    RETURN_IF_FAILED(MediaFoundationStreamWrapper::Create(
        stream_id++, this, demuxer_stream, task_runner, &mf_stream));
    media_streams_.push_back(mf_stream);
  }

  RETURN_IF_FAILED(MFCreateEventQueue(&mf_media_event_queue_));
  playback_element_id_ = playback_element_id;
  return S_OK;
}

void MediaFoundationSourceWrapper::DetachResource() {
  DVLOG(1) << __func__ << ": this=" << this;

  for (auto stream : media_streams_) {
    stream->DetachDemuxerStream();
  }
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetCharacteristics(
    DWORD* characteristics) {
  DVLOG(3) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }
  *characteristics = MFMEDIASOURCE_CAN_SEEK;
  return S_OK;
}

HRESULT MediaFoundationSourceWrapper::SelectDefaultStreams(
    const DWORD stream_desc_count,
    IMFPresentationDescriptor* presentation_descriptor) {
  bool audio_stream_selected = false;
  bool video_stream_selected = false;
  for (DWORD idx = 0; idx < stream_desc_count; idx++) {
    ComPtr<IMFStreamDescriptor> stream_descriptor;
    BOOL selected;
    RETURN_IF_FAILED(presentation_descriptor->GetStreamDescriptorByIndex(
        idx, &selected, &stream_descriptor));
    if (selected)
      continue;
    DWORD stream_id;
    RETURN_IF_FAILED(stream_descriptor->GetStreamIdentifier(&stream_id));
    if (media_streams_[stream_id]->StreamType() == DemuxerStream::Type::AUDIO &&
        !audio_stream_selected) {
      audio_stream_selected = true;
      RETURN_IF_FAILED(presentation_descriptor->SelectStream(idx));
    } else if (media_streams_[stream_id]->StreamType() ==
                   DemuxerStream::Type::VIDEO &&
               !video_stream_selected) {
      video_stream_selected = true;
      RETURN_IF_FAILED(presentation_descriptor->SelectStream(idx));
    }
  }
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::CreatePresentationDescriptor(
    IMFPresentationDescriptor** presentation_descriptor_out) {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }

  ComPtr<IMFPresentationDescriptor> presentation_descriptor;
  std::vector<ComPtr<IMFStreamDescriptor>> stream_descriptors;
  for (auto stream : media_streams_) {
    ComPtr<IMFStreamDescriptor> stream_descriptor;
    RETURN_IF_FAILED(stream->GetStreamDescriptor(&stream_descriptor));
    stream_descriptors.push_back(stream_descriptor);
  }
  const DWORD stream_desc_count = static_cast<DWORD>(stream_descriptors.size());
  RETURN_IF_FAILED(MFCreatePresentationDescriptor(
      stream_desc_count,
      reinterpret_cast<IMFStreamDescriptor**>(stream_descriptors.data()),
      &presentation_descriptor));
  RETURN_IF_FAILED(
      SelectDefaultStreams(stream_desc_count, presentation_descriptor.Get()));

  *presentation_descriptor_out = presentation_descriptor.Detach();
  return S_OK;
}

// https://docs.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-imfmediasource-start
IFACEMETHODIMP MediaFoundationSourceWrapper::Start(
    IMFPresentationDescriptor* presentation_descriptor,
    const GUID* guid_time_format,
    const PROPVARIANT* start_position) {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }

  bool seeked = false;
  bool started = false;

  // - If the previous state was stopped, the source sends an MESourceStarted
  // event.
  // - If the previous state was started or paused and the starting position is
  // the current position(VT_EMPTY), the source sends an MESourceStarted event.
  // - If the previous state was started or paused, and a new starting position
  // is specified, the source sends an MESourceSeeked event.
  if (state_ == State::kInitialized || state_ == State::kStopped) {
    started = true;
  } else if (state_ == State::kPaused || state_ == State::kStarted) {
    if (start_position->vt == VT_EMPTY)
      started = true;
    else
      seeked = true;
  }

  // - For each new stream, the source sends an MENewStream event. This event is
  // sent for the first Start call in which the stream appears. The event data
  // is a pointer to the stream's IMFMediaStream interface.
  // - For each updated stream, the source sends an MEUpdatedStream event. A
  // stream is updated if the stream already existed when Start was called (for
  // example, if the application seeks during playback).
  DWORD stream_desc_count = 0;
  RETURN_IF_FAILED(
      presentation_descriptor->GetStreamDescriptorCount(&stream_desc_count));

  for (DWORD i = 0; i < stream_desc_count; i++) {
    ComPtr<IMFStreamDescriptor> stream_descriptor;
    BOOL selected;
    RETURN_IF_FAILED(presentation_descriptor->GetStreamDescriptorByIndex(
        i, &selected, &stream_descriptor));

    DWORD stream_id;
    RETURN_IF_FAILED(stream_descriptor->GetStreamIdentifier(&stream_id));

    if (stream_id >= media_streams_.size()) {
      DLOG(ERROR) << "Unexpected stream id in descriptor: " << stream_id;
      continue;
    }

    ComPtr<MediaFoundationStreamWrapper> stream = media_streams_[stream_id];
    stream->SetFlushed(false);
    if (selected) {
      MediaEventType event_type = MENewStream;
      if (stream->IsSelected()) {
        event_type = MEUpdatedStream;
      }
      ComPtr<IUnknown> unknown_stream;
      RETURN_IF_FAILED(stream.As(&unknown_stream));
      RETURN_IF_FAILED(mf_media_event_queue_->QueueEventParamUnk(
          event_type, GUID_NULL, S_OK, unknown_stream.Get()));

      // - If the source sends an MESourceStarted event, each media stream
      // sends an MEStreamStarted event. If the source sends an MESourceSeeked
      // event, each stream sends an MEStreamSeeked event.
      if (started) {
        DCHECK(!seeked);
        RETURN_IF_FAILED(stream->QueueStartedEvent(start_position));
      } else if (seeked) {
        RETURN_IF_FAILED(stream->QueueSeekedEvent(start_position));
      }
    }
    stream->SetSelected(selected);
  }

  if (started) {
    DCHECK(!seeked);
    ComPtr<IMFMediaEvent> media_event;
    RETURN_IF_FAILED(MFCreateMediaEvent(MESourceStarted, GUID_NULL, S_OK,
                                        start_position, &media_event));
    RETURN_IF_FAILED(mf_media_event_queue_->QueueEvent(media_event.Get()));
  } else if (seeked) {
    RETURN_IF_FAILED(
        QueueEvent(MESourceSeeked, GUID_NULL, S_OK, start_position));
  }

  state_ = State::kStarted;
  presentation_ended_ = false;
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::Stop() {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }

  RETURN_IF_FAILED(QueueEvent(MESourceStopped, GUID_NULL, S_OK, nullptr));

  for (auto stream : media_streams_) {
    if (stream->IsSelected()) {
      stream->QueueStoppedEvent();
    }
  }

  state_ = State::kStopped;
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::Pause() {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }
  if (state_ != State::kStarted) {
    DLOG(ERROR) << __func__ << ": MF_E_INVALID_STATE_TRANSITION";
    return MF_E_INVALID_STATE_TRANSITION;
  }

  RETURN_IF_FAILED(QueueEvent(MESourcePaused, GUID_NULL, S_OK, nullptr));

  for (auto stream : media_streams_) {
    if (stream->IsSelected()) {
      stream->QueuePausedEvent();
    }
  }

  state_ = State::kPaused;
  return S_OK;
}

// After this method is called, methods on the media source and all of its
// media streams return MF_E_SHUTDOWN (except for IUnknown methods).
IFACEMETHODIMP MediaFoundationSourceWrapper::Shutdown() {
  DVLOG(1) << __func__ << ": this=" << this;

  for (auto stream : media_streams_) {
    stream->DetachParent();
  }
  state_ = State::kShutdown;
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetEvent(
    DWORD flags,
    IMFMediaEvent** event_out) {
  DVLOG(3) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }
  // Not tracing hr to avoid the noise from MF_E_NO_EVENTS_AVAILABLE.
  return mf_media_event_queue_->GetEvent(flags, event_out);
}

IFACEMETHODIMP MediaFoundationSourceWrapper::BeginGetEvent(
    IMFAsyncCallback* callback,
    IUnknown* state) {
  DVLOG(3) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }
  RETURN_IF_FAILED(mf_media_event_queue_->BeginGetEvent(callback, state));
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::EndGetEvent(
    IMFAsyncResult* result,
    IMFMediaEvent** event_out) {
  DVLOG(3) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN";
    return MF_E_SHUTDOWN;
  }
  RETURN_IF_FAILED(mf_media_event_queue_->EndGetEvent(result, event_out));
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::QueueEvent(
    MediaEventType type,
    REFGUID extended_type,
    HRESULT status,
    const PROPVARIANT* value) {
  DVLOG(3) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown) {
    DLOG(ERROR) << __func__ << ": MF_E_SHUTDOWN, type=" << type;
    return MF_E_SHUTDOWN;
  }
  RETURN_IF_FAILED(mf_media_event_queue_->QueueEventParamVar(
      type, extended_type, status, value));
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetInputTrustAuthority(
    DWORD stream_id,
    REFIID riid,
    IUnknown** object_out) {
  DVLOG(1) << __func__ << ": this=" << this;

  if (stream_id >= StreamCount())
    return E_INVALIDARG;

  if (!cdm_proxy_) {
    DLOG(ERROR) << __func__ << ": MF_E_NOT_PROTECTED";
    return MF_E_NOT_PROTECTED;
  }

  if (!media_streams_[stream_id]->IsEncrypted()) {
    DVLOG(1) << __func__ << ". Unprotected stream. stream_id=" << stream_id
             << ",this=" << this;
    return MF_E_NOT_PROTECTED;
  }

  // Use |nullptr| for content init_data and |0| for its size.
  RETURN_IF_FAILED(cdm_proxy_->GetInputTrustAuthority(
      playback_element_id_, stream_id, StreamCount(), nullptr, 0, riid,
      object_out));
  return S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetService(REFGUID guid_service,
                                                        REFIID riid,
                                                        LPVOID* result) {
  DVLOG(3) << __func__ << ": this=" << this;
  DCHECK(result);

  if (!IsEqualGUID(guid_service, MF_RATE_CONTROL_SERVICE))
    return MF_E_UNSUPPORTED_SERVICE;
  return QueryInterface(riid, result);
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetSlowestRate(
    MFRATE_DIRECTION direction,
    BOOL supports_thinning,
    float* rate) {
  DVLOG(2) << __func__ << ": this=" << this;
  DCHECK(rate);

  if (direction == MFRATE_REVERSE) {
    return MF_E_REVERSE_UNSUPPORTED;
  }
  *rate = 0.0f;
  return state_ == State::kShutdown ? MF_E_SHUTDOWN : S_OK;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetFastestRate(
    MFRATE_DIRECTION direction,
    BOOL supports_thinning,
    float* rate) {
  DVLOG(2) << __func__ << ": this=" << this;
  DCHECK(rate);

  if (direction == MFRATE_REVERSE) {
    return MF_E_REVERSE_UNSUPPORTED;
  }
  *rate = 0.0f;
  HRESULT hr = MF_E_SHUTDOWN;
  if (state_ != State::kShutdown) {
    *rate = 8.0f;
    hr = S_OK;
  }
  return hr;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::IsRateSupported(
    BOOL supports_thinning,
    float new_rate,
    float* supported_rate) {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown)
    return MF_E_SHUTDOWN;

  if (supported_rate)
    *supported_rate = 0.0f;

  MFRATE_DIRECTION direction =
      (new_rate >= 0) ? MFRATE_FORWARD : MFRATE_REVERSE;
  float fastest_rate = 0.0f;
  float slowest_rate = 0.0f;

  GetFastestRate(direction, supports_thinning, &fastest_rate);
  GetSlowestRate(direction, supports_thinning, &slowest_rate);

  HRESULT hr;
  if (supports_thinning) {
    // We do not support thinning right now. If Thin is supported, this
    // MediaSource is expected to drop all samples except ones marked as
    // MFSampleExtension_CleanPoint
    hr = MF_E_THINNING_UNSUPPORTED;
  } else if (new_rate < slowest_rate) {
    hr = MF_E_REVERSE_UNSUPPORTED;
  } else if (new_rate > fastest_rate) {
    hr = MF_E_UNSUPPORTED_RATE;
  } else {
    // Supported
    hr = S_OK;
    if (supported_rate) {
      *supported_rate = new_rate;
    }
  }

  return hr;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::SetRate(BOOL supports_thinning,
                                                     float rate) {
  DVLOG(2) << __func__ << ": this=" << this;

  if (state_ == State::kShutdown)
    return MF_E_SHUTDOWN;

  HRESULT hr = IsRateSupported(supports_thinning, rate, &current_rate_);
  if (SUCCEEDED(hr)) {
    PROPVARIANT varRate;
    varRate.vt = VT_R4;
    varRate.fltVal = current_rate_;
    hr = QueueEvent(MESourceRateChanged, GUID_NULL, S_OK, &varRate);
  }
  return hr;
}

IFACEMETHODIMP MediaFoundationSourceWrapper::GetRate(BOOL* supports_thinning,
                                                     float* rate) {
  DVLOG(2) << __func__ << ": this=" << this;

  *supports_thinning = FALSE;
  *rate = 0.0f;
  HRESULT hr = MF_E_SHUTDOWN;
  if (state_ != State::kShutdown) {
    hr = S_OK;
    *rate = current_rate_;
  }
  return hr;
}

uint32_t MediaFoundationSourceWrapper::StreamCount() const {
  return media_streams_.size();
}

void MediaFoundationSourceWrapper::CheckForEndOfPresentation() {
  DVLOG(2) << __func__ << ": this=" << this;
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (presentation_ended_) {
    return;
  }

  bool presentation_ended = true;
  for (auto mf_stream : media_streams_) {
    if (!mf_stream->HasEnded()) {
      presentation_ended = false;
      break;
    }
  }
  presentation_ended_ = presentation_ended;
  if (presentation_ended_) {
    HRESULT hr = QueueEvent(MEEndOfPresentation, GUID_NULL, S_OK, nullptr);
    DLOG_IF(ERROR, FAILED(hr))
        << "Failed to notify end of presentation. hr=" << hr;
  }
}

bool MediaFoundationSourceWrapper::HasEncryptedStream() const {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  for (auto mf_stream : media_streams_) {
    if (mf_stream->IsEncrypted()) {
      return true;
    }
  }
  return false;
}

void MediaFoundationSourceWrapper::SetCdmProxy(IMFCdmProxy* cdm_proxy) {
  DVLOG(2) << __func__ << ": this=" << this;
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // cdm_proxy_ should never change.
  DCHECK(!cdm_proxy_);
  cdm_proxy_ = cdm_proxy;

  HRESULT hr = cdm_proxy_->RefreshTrustedInput(playback_element_id_);
  DLOG_IF(ERROR, FAILED(hr)) << "Failed to refresh trusted input. hr=" << hr;
}

bool MediaFoundationSourceWrapper::SetVideoStreamEnabled(bool enabled) {
  DVLOG(2) << __func__ << ": this=" << this << ",enabled=" << enabled;
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (enabled == video_stream_enabled_)
    return false;

  video_stream_enabled_ = enabled;

  for (auto stream : media_streams_) {
    if (stream->IsSelected() &&
        stream->StreamType() == DemuxerStream::Type::VIDEO) {
      stream->SetEnabled(enabled);
      if (enabled && stream->HasEnded()) {
        // Need to restart (pause+play) if we're re-enabling a video stream
        // which has previously reached EOS.
        return true;
      }
    }
  }
  return false;
}

void MediaFoundationSourceWrapper::FlushStreams() {
  DVLOG(2) << __func__ << ": this=" << this;
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  for (auto stream : media_streams_) {
    stream->SetFlushed(true);
  }
}

}  // namespace media
