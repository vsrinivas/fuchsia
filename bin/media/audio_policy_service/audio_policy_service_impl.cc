// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_policy_service/audio_policy_service_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zx/time.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"

namespace media {
namespace {

static constexpr float kMaxSystemAudioGain = 0.0f;
static constexpr uint32_t kInitializeAttempts = 30;
static constexpr zx::duration kInitializeAttemptInterval = zx::msec(100);
static const std::string kStatusFilePath =
    "/data/app_local/audio_policy_service/status";
static const std::string kStatusFileDir =
    "/data/app_local/audio_policy_service";

}  // namespace

AudioPolicyServiceImpl::AudioPolicyServiceImpl(
    std::unique_ptr<component::ApplicationContext> application_context)
    : application_context_(std::move(application_context)),
      initialize_attempts_remaining_(kInitializeAttempts) {
  application_context_->outgoing_services()->AddService<AudioPolicyService>(
      [this](fidl::InterfaceRequest<AudioPolicyService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  status_publisher_.SetCallbackRunner(
      [this](GetStatusCallback callback, uint64_t version) {
        callback(version, Status());
      });

  InitializeAudioService();
}

AudioPolicyServiceImpl::~AudioPolicyServiceImpl() {}

void AudioPolicyServiceImpl::GetStatus(uint64_t version_last_seen,
                                       GetStatusCallback callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void AudioPolicyServiceImpl::SetSystemAudioGain(float db) {
  db = std::max(std::min(db, kMaxSystemAudioGain), kMutedGain);

  if (system_audio_gain_db_ == db) {
    return;
  }

  if (db == kMutedGain) {
    // System audio gain is being set to |kMutedGain|. This implicitly mutes
    // system audio.
    system_audio_muted_ = true;
  } else if (system_audio_gain_db_ == kMutedGain) {
    // System audio was muted, because gain was set to |kMutedGain|. We're
    // raising the gain now, so we unmute.
    system_audio_muted_ = false;
  }

  system_audio_gain_db_ = db;

  UpdateAudioService();
  status_publisher_.SendUpdates();
  SaveStatus();
}

void AudioPolicyServiceImpl::SetSystemAudioMute(bool muted) {
  if (system_audio_gain_db_ == kMutedGain) {
    // Keep audio muted if system audio gain is set to |kMutedGain|.
    muted = true;
  }

  if (system_audio_muted_ == muted) {
    return;
  }

  system_audio_muted_ = muted;

  UpdateAudioService();
  status_publisher_.SendUpdates();
  SaveStatus();
}

void AudioPolicyServiceImpl::InitializeAudioService() {
  // The file system isn't always ready when this service is started, so we
  // make a series of attempts to find the status file. If that fails, we give
  // up and use the defaults.

  if (!files::IsFile(kStatusFilePath) &&
      --initialize_attempts_remaining_ != 0) {
    async::PostDelayedTask(async_get_default(),
                           [this]() { InitializeAudioService(); },
                           kInitializeAttemptInterval);
    return;
  }

  LoadStatus();
  UpdateAudioService();
  status_publisher_.SendUpdates();
  SaveStatus();
}

AudioPolicyStatus AudioPolicyServiceImpl::Status() {
  AudioPolicyStatus status;
  status.system_audio_gain_db = system_audio_gain_db_;
  status.system_audio_muted = system_audio_muted_;
  return status;
}

void AudioPolicyServiceImpl::LoadStatus() {
  std::vector<uint8_t> buffer;

  if (!files::ReadFileToVector(kStatusFilePath, &buffer)) {
    FXL_LOG(WARNING) << "Failed to read status";
    return;
  }

  AudioPolicyStatus status;

  if (buffer.size() != sizeof(status)) {
    FXL_LOG(WARNING) << "Failed to deserialize status";
    return;
  }

  memcpy(&status, buffer.data(), sizeof(status));

  system_audio_gain_db_ = status.system_audio_gain_db;
  system_audio_muted_ = status.system_audio_muted;
}

void AudioPolicyServiceImpl::SaveStatus() {
  AudioPolicyStatus status = Status();

  if (!files::IsDirectory(kStatusFileDir) &&
      !files::CreateDirectory(kStatusFileDir)) {
    FXL_LOG(WARNING) << "Failed to create directory " << kStatusFileDir;
  }

  if (!files::WriteFile(kStatusFilePath, reinterpret_cast<const char*>(&status),
                        sizeof(status))) {
    FXL_LOG(WARNING) << "Failed to write status to " << kStatusFilePath;
    return;
  }
}

void AudioPolicyServiceImpl::UpdateAudioService() {
  EnsureAudioService();
  audio_service_->SetMasterGain(effective_system_audio_gain());
}

void AudioPolicyServiceImpl::EnsureAudioService() {
  if (audio_service_) {
    return;
  }

  audio_service_ =
      application_context_->ConnectToEnvironmentService<AudioServer>();

  audio_service_.set_error_handler([this]() {
    audio_service_.set_error_handler(nullptr);
    audio_service_.Unbind();
  });
}

}  // namespace media
