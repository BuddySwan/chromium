// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/audio/input_stream.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "media/audio/audio_manager.h"
#include "media/base/audio_parameters.h"
#include "media/base/user_input_monitor.h"
#include "mojo/public/cpp/system/buffer.h"
#include "mojo/public/cpp/system/handle.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "services/audio/input_sync_writer.h"
#include "services/audio/user_input_monitor.h"

namespace audio {

namespace {
const int kMaxInputChannels = 3;

const char* ErrorCodeToString(InputController::ErrorCode error) {
  switch (error) {
    case (InputController::STREAM_CREATE_ERROR):
      return "STREAM_CREATE_ERROR";
    case (InputController::STREAM_OPEN_ERROR):
      return "STREAM_OPEN_ERROR";
    case (InputController::STREAM_ERROR):
      return "STREAM_ERROR";
    default:
      NOTREACHED();
  }
  return "UNKNOWN_ERROR";
}

std::string GetCtorLogString(const base::UnguessableToken& id,
                             const std::string& device_id,
                             const media::AudioParameters& params,
                             bool enable_agc) {
  std::string str = base::StringPrintf("Ctor({id=%s}, ", id.ToString().c_str());
  base::StringAppendF(&str, "{device_id=%s}, ", device_id.c_str());
  base::StringAppendF(&str, "{params=[%s]}, ",
                      params.AsHumanReadableString().c_str());
  base::StringAppendF(&str, "{enable_agc=%d})", enable_agc);
  return str;
}

}  // namespace

InputStream::InputStream(
    CreatedCallback created_callback,
    DeleteCallback delete_callback,
    mojo::PendingReceiver<media::mojom::AudioInputStream> receiver,
    mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
    mojo::PendingRemote<media::mojom::AudioInputStreamObserver> observer,
    mojo::PendingRemote<media::mojom::AudioLog> log,
    media::AudioManager* audio_manager,
    std::unique_ptr<UserInputMonitor> user_input_monitor,
    const std::string& device_id,
    const media::AudioParameters& params,
    uint32_t shared_memory_count,
    bool enable_agc,
    StreamMonitorCoordinator* stream_monitor_coordinator,
    mojom::AudioProcessingConfigPtr processing_config)
    : id_(base::UnguessableToken::Create()),
      receiver_(this, std::move(receiver)),
      client_(std::move(client)),
      observer_(std::move(observer)),
      log_(std::move(log)),
      created_callback_(std::move(created_callback)),
      delete_callback_(std::move(delete_callback)),
      foreign_socket_(),
      writer_(InputSyncWriter::Create(
          log_ ? base::BindRepeating(&media::mojom::AudioLog::OnLogMessage,
                                     base::Unretained(log_.get()))
               : base::DoNothing(),
          shared_memory_count,
          params,
          &foreign_socket_)),
      user_input_monitor_(std::move(user_input_monitor)) {
  DCHECK(audio_manager);
  DCHECK(receiver_.is_bound());
  DCHECK(client_);
  DCHECK(created_callback_);
  DCHECK(delete_callback_);
  DCHECK(params.IsValid());
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("audio", "audio::InputStream", this);
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2("audio", "InputStream", this, "device id",
                                    device_id, "params",
                                    params.AsHumanReadableString());
  SendLogMessage(GetCtorLogString(id_, device_id, params, enable_agc));

  // |this| owns these objects, so unretained is safe.
  base::RepeatingClosure error_handler = base::BindRepeating(
      &InputStream::OnStreamError, base::Unretained(this), false);
  receiver_.set_disconnect_handler(error_handler);
  client_.set_disconnect_handler(error_handler);

  if (observer_)
    observer_.set_disconnect_handler(std::move(error_handler));

  if (log_) {
    log_->OnCreated(params, device_id);
    if (processing_config)
      log_->OnProcessingStateChanged(processing_config->settings.ToString());
  }

  // Only MONO, STEREO and STEREO_AND_KEYBOARD_MIC channel layouts are expected,
  // see AudioManagerBase::MakeAudioInputStream().
  if (params.channels() > kMaxInputChannels) {
    OnStreamError(true);
    return;
  }

  if (!writer_) {
    OnStreamError(true);
    return;
  }

  controller_ = InputController::Create(
      audio_manager, this, writer_.get(), user_input_monitor_.get(), params,
      device_id, enable_agc, stream_monitor_coordinator,
      std::move(processing_config));
}

InputStream::~InputStream() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SendLogMessage("Dtor({id=" + id_.ToString() + "})");

  if (log_)
    log_->OnClosed();

  if (observer_) {
    observer_.ResetWithReason(
        static_cast<uint32_t>(media::mojom::AudioInputStreamObserver::
                                  DisconnectReason::kTerminatedByClient),
        std::string());
  }

  if (created_callback_) {
    // Didn't manage to create the stream. Call the callback anyways as mandated
    // by mojo.
    std::move(created_callback_).Run(nullptr, false, base::nullopt);
  }

  if (!controller_) {
    // Didn't initialize properly, nothing to clean up.
    return;
  }

  // TODO(https://crbug.com/803102): remove InputController::Close() after
  // content/ streams are removed, destructor should suffice.
  controller_->Close();

  TRACE_EVENT_NESTABLE_ASYNC_END0("audio", "InputStream", this);
  TRACE_EVENT_NESTABLE_ASYNC_END0("audio", "audio::InputStream", this);
}

void InputStream::SetOutputDeviceForAec(const std::string& output_device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(controller_);
  controller_->SetOutputDeviceForAec(output_device_id);
  if (log_) {
    log_->OnLogMessage(
        base::StrCat({"SetOutputDeviceForAec: ", output_device_id}));
  }
  SendLogMessage("SetOutputDeviceForAec({id=" + id_.ToString() + "}, " +
                 "{output_device_id=" + output_device_id + "})");
}

void InputStream::Record() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(controller_);
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT0("audio", "Record", this);
  SendLogMessage("Record({id=" + id_.ToString() + "})");

  controller_->Record();
  if (observer_)
    observer_->DidStartRecording();
  if (log_)
    log_->OnStarted();
}

void InputStream::SetVolume(double volume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(controller_);
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT1("audio", "SetVolume", this, "volume",
                                      volume);

  if (volume < 0 || volume > 1) {
    mojo::ReportBadMessage("Invalid volume");
    OnStreamError(true);
    return;
  }

  controller_->SetVolume(volume);
  if (log_)
    log_->OnSetVolume(volume);
}

void InputStream::OnCreated(bool initially_muted) {
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT1("audio", "Created", this,
                                      "initially muted", initially_muted);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SendLogMessage(base::StringPrintf("OnCreated({id=%s}, {muted=%d})",
                                    id_.ToString().c_str(), initially_muted));

  base::ReadOnlySharedMemoryRegion shared_memory_region =
      writer_->TakeSharedMemoryRegion();
  if (!shared_memory_region.IsValid()) {
    OnStreamError(true);
    return;
  }

  mojo::PlatformHandle socket_handle(foreign_socket_.Take());
  DCHECK(socket_handle.is_valid());

  std::move(created_callback_)
      .Run({base::in_place, std::move(shared_memory_region),
            std::move(socket_handle)},
           initially_muted, id_);
}

void InputStream::OnError(InputController::ErrorCode error_code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT0("audio", "Error", this);

  client_->OnError();
  if (log_) {
    log_->OnError();
    SendLogMessage(base::StringPrintf("OnError({id=%s}, {error_code=%s})",
                                      id_.ToString().c_str(),
                                      ErrorCodeToString(error_code)));
  }
  OnStreamError(true);
}

void InputStream::OnLog(base::StringPiece message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  if (log_)
    log_->OnLogMessage(message.as_string() + " [id=" + id_.ToString() + "]");
}

void InputStream::OnMuted(bool is_muted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  client_->OnMutedStateChanged(is_muted);
}

void InputStream::OnStreamError(bool signalPlatformError) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT0("audio", "OnStreamError", this);

  if (signalPlatformError && observer_) {
    observer_.ResetWithReason(
        static_cast<uint32_t>(media::mojom::AudioInputStreamObserver::
                                  DisconnectReason::kPlatformError),
        std::string());
  }

  if (signalPlatformError) {
    SendLogMessage("OnStreamError({id=" + id_.ToString() + "})");
  }

  // Defer callback so we're not destructed while in the constructor.
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&InputStream::CallDeleter, weak_factory_.GetWeakPtr()));
  receiver_.reset();
}

void InputStream::CallDeleter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);

  std::move(delete_callback_).Run(this);
}

void InputStream::SendLogMessage(const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  if (log_) {
    log_->OnLogMessage("audio::IS::" + message);
  }
}

}  // namespace audio
