// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/pulse/pulse_output.h"

#include <pulse/pulseaudio.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "media/audio/audio_device_description.h"
#include "media/audio/audio_manager_base.h"
#include "media/audio/pulse/pulse_util.h"
#include "media/base/audio_sample_types.h"

namespace media {

namespace {

PRINTF_FORMAT(2, 3)
void SendLogMessage(const AudioManagerBase::LogCallback& callback,
                    const char* format,
                    ...) {
  if (callback.is_null())
    return;
  va_list args;
  va_start(args, format);
  callback.Run("PAOS::" + base::StringPrintV(format, args));
  va_end(args);
}

}  // namespace

using pulse::AutoPulseLock;
using pulse::WaitForOperationCompletion;

// static, pa_stream_notify_cb
void PulseAudioOutputStream::StreamNotifyCallback(pa_stream* s, void* p_this) {
  PulseAudioOutputStream* stream = static_cast<PulseAudioOutputStream*>(p_this);

  // Forward unexpected failures to the AudioSourceCallback if available.  All
  // these variables are only modified under pa_threaded_mainloop_lock() so this
  // should be thread safe.
  if (s && stream->source_callback_ &&
      pa_stream_get_state(s) == PA_STREAM_FAILED) {
    stream->source_callback_->OnError(AudioSourceCallback::ErrorType::kUnknown);
  }

  pa_threaded_mainloop_signal(stream->pa_mainloop_, 0);
}

// static, pa_stream_request_cb_t
void PulseAudioOutputStream::StreamRequestCallback(pa_stream* s, size_t len,
                                                   void* p_this) {
  // Fulfill write request; must always result in a pa_stream_write() call.
  static_cast<PulseAudioOutputStream*>(p_this)->FulfillWriteRequest(len);
}

PulseAudioOutputStream::PulseAudioOutputStream(
    const AudioParameters& params,
    const std::string& device_id,
    AudioManagerBase* manager,
    const AudioManager::LogCallback log_callback)
    : params_(AudioParameters(params.format(),
                              params.channel_layout(),
                              params.sample_rate(),
                              params.frames_per_buffer())),
      device_id_(device_id),
      manager_(manager),
      log_callback_(std::move(log_callback)),
      pa_context_(nullptr),
      pa_mainloop_(nullptr),
      pa_stream_(nullptr),
      volume_(1.0f),
      source_callback_(nullptr),
      buffer_size_(params_.GetBytesPerBuffer(kSampleFormatF32)) {
  CHECK(params_.IsValid());
  SendLogMessage(
      log_callback_,
      "PulseAudioOutputStream({device_id=%s}, {params=[%s]} [this=%p])",
      device_id.c_str(), params.AsHumanReadableString().c_str(), this);
  audio_bus_ = AudioBus::Create(params_);
}

PulseAudioOutputStream::~PulseAudioOutputStream() {
  // All internal structures should already have been freed in Close(), which
  // calls AudioManagerBase::ReleaseOutputStream() which deletes this object.
  DCHECK(!pa_stream_);
  DCHECK(!pa_context_);
  DCHECK(!pa_mainloop_);
}

bool PulseAudioOutputStream::Open() {
  DCHECK(thread_checker_.CalledOnValidThread());
  SendLogMessage(log_callback_, "Open([this=%p])", this);
  bool result = pulse::CreateOutputStream(
      &pa_mainloop_, &pa_context_, &pa_stream_, params_, device_id_,
      AudioManager::GetGlobalAppName(), &StreamNotifyCallback,
      &StreamRequestCallback, this);
  if (!result) {
    SendLogMessage(log_callback_, "Open => (ERROR: failed to open PA stream)");
  }
  return result;
}

void PulseAudioOutputStream::Reset() {
  if (!pa_mainloop_) {
    DCHECK(!pa_stream_);
    DCHECK(!pa_context_);
    return;
  }

  {
    AutoPulseLock auto_lock(pa_mainloop_);

    // Close the stream.
    if (pa_stream_) {
      // Ensure all samples are played out before shutdown.
      pa_operation* operation = pa_stream_flush(
          pa_stream_, &pulse::StreamSuccessCallback, pa_mainloop_);
      WaitForOperationCompletion(pa_mainloop_, operation, pa_context_,
                                 pa_stream_);

      // Release PulseAudio structures.
      pa_stream_disconnect(pa_stream_);
      pa_stream_set_write_callback(pa_stream_, nullptr, nullptr);
      pa_stream_set_state_callback(pa_stream_, nullptr, nullptr);
      pa_stream_unref(pa_stream_);
      pa_stream_ = nullptr;
    }

    if (pa_context_) {
      pa_context_disconnect(pa_context_);
      pa_context_set_state_callback(pa_context_, nullptr, nullptr);
      pa_context_unref(pa_context_);
      pa_context_ = nullptr;
    }
  }

  pa_threaded_mainloop_stop(pa_mainloop_);
  pa_threaded_mainloop_free(pa_mainloop_);
  pa_mainloop_ = nullptr;
}

void PulseAudioOutputStream::Close() {
  DCHECK(thread_checker_.CalledOnValidThread());
  SendLogMessage(log_callback_, "Close([this=%p])", this);

  Reset();

  // Signal to the manager that we're closed and can be removed.
  // This should be the last call in the function as it deletes "this".
  manager_->ReleaseOutputStream(this);
}

// This stream is always used with sub second buffer sizes, where it's
// sufficient to simply always flush upon Start().
void PulseAudioOutputStream::Flush() {}

void PulseAudioOutputStream::FulfillWriteRequest(size_t requested_bytes) {
  int bytes_remaining = requested_bytes;
  while (bytes_remaining > 0) {
    void* pa_buffer = nullptr;
    size_t pa_buffer_size = buffer_size_;
    CHECK_GE(pa_stream_begin_write(pa_stream_, &pa_buffer, &pa_buffer_size), 0);

    if (!source_callback_) {
      memset(pa_buffer, 0, pa_buffer_size);
      pa_stream_write(pa_stream_, pa_buffer, pa_buffer_size, nullptr, 0LL,
                      PA_SEEK_RELATIVE);
      bytes_remaining -= pa_buffer_size;
      continue;
    }

    size_t unwritten_frames_in_bus = audio_bus_->frames();
    size_t frames_filled = source_callback_->OnMoreData(
        pulse::GetHardwareLatency(pa_stream_), base::TimeTicks::Now(), 0,
        audio_bus_.get());

    // Zero any unfilled data so it plays back as silence.
    if (frames_filled < unwritten_frames_in_bus) {
      audio_bus_->ZeroFramesPartial(frames_filled,
                                    unwritten_frames_in_bus - frames_filled);
    }

    audio_bus_->Scale(volume_);

    size_t frame_size = buffer_size_ / unwritten_frames_in_bus;
    size_t frames_to_copy = pa_buffer_size / frame_size;
    size_t frame_offset_in_bus = 0;
    do {
      // Grab frames and get the count.
      frames_to_copy =
          std::min(audio_bus_->frames() - frame_offset_in_bus, frames_to_copy);

      // We skip clipping since that occurs at the shared memory boundary.
      audio_bus_->ToInterleavedPartial<Float32SampleTypeTraitsNoClip>(
          frame_offset_in_bus, frames_to_copy,
          reinterpret_cast<float*>(pa_buffer));
      frame_offset_in_bus += frames_to_copy;
      unwritten_frames_in_bus -= frames_to_copy;

      if (pa_stream_write(pa_stream_, pa_buffer, pa_buffer_size, nullptr, 0LL,
                          PA_SEEK_RELATIVE) < 0) {
        source_callback_->OnError(AudioSourceCallback::ErrorType::kUnknown);
        return;
      }
      bytes_remaining -= pa_buffer_size;
      if (unwritten_frames_in_bus) {
        // Reset the buffer and the size:
        //   - If pa_buffer isn't nulled out, then it will get re-used, and
        //     there will be a race between PA reading and us writing.
        //   - If we don't shrink the pa_buffer_size to a small value, we get
        //     stuttering as the memory allocation can take far too long. This
        //     also means that we will never get more than we want, and we
        //     dont need to memset.
        pa_buffer = nullptr;
        pa_buffer_size = unwritten_frames_in_bus * frame_size;
        CHECK_GE(pa_stream_begin_write(pa_stream_, &pa_buffer, &pa_buffer_size),
                 0);
        frames_to_copy = pa_buffer_size / frame_size;
      }
    } while (unwritten_frames_in_bus);
  }
}

void PulseAudioOutputStream::Start(AudioSourceCallback* callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CHECK(callback);
  CHECK(pa_stream_);
  SendLogMessage(log_callback_, "Start([this=%p])", this);

  AutoPulseLock auto_lock(pa_mainloop_);

  // Ensure the context and stream are ready.
  if (pa_context_get_state(pa_context_) != PA_CONTEXT_READY &&
      pa_stream_get_state(pa_stream_) != PA_STREAM_READY) {
    callback->OnError(AudioSourceCallback::ErrorType::kUnknown);
    return;
  }

  source_callback_ = callback;

  // Uncork (resume) the stream.
  pa_operation* operation = pa_stream_cork(
      pa_stream_, 0, &pulse::StreamSuccessCallback, pa_mainloop_);
  if (!WaitForOperationCompletion(pa_mainloop_, operation, pa_context_,
                                  pa_stream_)) {
    callback->OnError(AudioSourceCallback::ErrorType::kUnknown);
  }
}

void PulseAudioOutputStream::Stop() {
  DCHECK(thread_checker_.CalledOnValidThread());
  SendLogMessage(log_callback_, "Stop([this=%p])", this);

  // Cork (pause) the stream.  Waiting for the main loop lock will ensure
  // outstanding callbacks have completed.
  AutoPulseLock auto_lock(pa_mainloop_);

  // Set |source_callback_| to nullptr so all FulfillWriteRequest() calls which
  // may occur while waiting on the flush and cork exit immediately.
  auto* callback = source_callback_;
  source_callback_ = nullptr;

  // Flush the stream prior to cork, doing so after will cause hangs.  Write
  // callbacks are suspended while inside pa_threaded_mainloop_lock() so this
  // is all thread safe.
  pa_operation* operation =
      pa_stream_flush(pa_stream_, &pulse::StreamSuccessCallback, pa_mainloop_);
  if (!WaitForOperationCompletion(pa_mainloop_, operation, pa_context_,
                                  pa_stream_)) {
    callback->OnError(AudioSourceCallback::ErrorType::kUnknown);
  }

  operation = pa_stream_cork(pa_stream_, 1, &pulse::StreamSuccessCallback,
                             pa_mainloop_);
  if (!WaitForOperationCompletion(pa_mainloop_, operation, pa_context_,
                                  pa_stream_)) {
    callback->OnError(AudioSourceCallback::ErrorType::kUnknown);
  }
}

void PulseAudioOutputStream::SetVolume(double volume) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Waiting for the main loop lock will ensure outstanding callbacks have
  // completed and |volume_| is not accessed from them.
  AutoPulseLock auto_lock(pa_mainloop_);
  volume_ = static_cast<float>(volume);
}

void PulseAudioOutputStream::GetVolume(double* volume) {
  DCHECK(thread_checker_.CalledOnValidThread());

  *volume = volume_;
}

}  // namespace media
