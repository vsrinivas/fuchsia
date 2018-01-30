// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "garnet/bin/media/tts_service/tts_service_impl.h"
#include "garnet/bin/media/tts_service/tts_speaker.h"
#include "lib/fsl/tasks/message_loop.h"
#include "third_party/flite/include/flite_fuchsia.h"

namespace media {
namespace tts {

TtsServiceImpl::TtsServiceImpl(
    std::unique_ptr<app::ApplicationContext> application_context)
    : application_context_(std::move(application_context)) {
  FXL_DCHECK(application_context_);

  application_context_->outgoing_services()->AddService<TtsService>(
      [this](fidl::InterfaceRequest<TtsService> request) {
        clients_.insert(new Client(this, std::move(request)));
      });

  // Stash a pointer to our task runner.
  FXL_DCHECK(fsl::MessageLoop::GetCurrent());
  task_runner_ = fsl::MessageLoop::GetCurrent()->task_runner();
  FXL_DCHECK(task_runner_);
}

TtsServiceImpl::~TtsServiceImpl() {
  FXL_DCHECK(clients_.size() == 0);
}

zx_status_t TtsServiceImpl::Init() {
  int res = flite_init();
  if (res < 0) {
    FXL_LOG(ERROR) << "Failed to initialize flite (res " << res << ")";
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

TtsServiceImpl::Client::Client(TtsServiceImpl* owner,
                               fidl::InterfaceRequest<TtsService> request)
    : owner_(owner), binding_(this, std::move(request)) {
  binding_.set_error_handler([this] { Shutdown(); });
}

TtsServiceImpl::Client::~Client() {
  FXL_DCHECK(active_speakers_.size() == 0);
  FXL_DCHECK(binding_.is_bound() == false);
}

void TtsServiceImpl::Client::Shutdown() {
  for (const auto& speaker : active_speakers_) {
    speaker->Shutdown();
  }

  binding_.Unbind();
  active_speakers_.clear();
  owner_->clients_.erase(owner_->clients_.find(this));
}

void TtsServiceImpl::Client::Say(const fidl::String& words,
                                 uint64_t token,
                                 const SayCallback& cbk) {
  auto cleanup = fbl::MakeAutoCall([this] { Shutdown(); });
  auto speaker = std::make_shared<TtsSpeaker>(owner_->task_runner_);

  if (speaker->Init(owner_->application_context_) != ZX_OK) {
    return;
  }

  fxl::Closure on_speak_complete = [this, speaker, token,
                                    say_callback = std::move(cbk)]() {
    OnSpeakComplete(std::move(speaker), token, std::move(say_callback));
  };

  zx_status_t res =
      speaker->Speak(std::move(words), std::move(on_speak_complete));
  if (res == ZX_OK) {
    active_speakers_.insert(std::move(speaker));
  } else {
    FXL_LOG(ERROR) << "Failed to start to speak (res " << res << ")";
    return;
  }

  cleanup.cancel();
}

void TtsServiceImpl::Client::OnSpeakComplete(
    std::shared_ptr<TtsSpeaker> speaker,
    uint64_t token,
    SayCallback cbk) {
  auto iter = active_speakers_.find(speaker);

  if (iter == active_speakers_.end()) {
    return;
  }

  speaker->Shutdown();
  active_speakers_.erase(iter);
  cbk(token);
}

}  // namespace tts
}  // namespace media
