// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_admission_control.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <mutex>

void CodecAdmissionControl::TryAddCodec(bool multi_instance,
                                        fit::function<void(std::unique_ptr<CodecAdmission>)>
                                            continue_after_previously_started_channel_closes_done) {
  PostAfterPreviouslyStartedClosesDone(
      [this, multi_instance,
       callback = std::move(continue_after_previously_started_channel_closes_done)] {
        callback(TryAddCodecInternal(multi_instance));
      });
}

void CodecAdmissionControl::PostAfterPreviouslyStartedClosesDone(fit::closure to_run) {
  zx_status_t result =
      async::PostTask(shared_fidl_dispatcher_, [this, to_run = std::move(to_run)]() mutable {
        for (auto& codec : codecs_to_check_for_close_) {
          // Needs to run outside the lock because it may call into OnCodecIsClosing, which takes
          // the lock.
          codec->CheckIfChannelClosing();
        }
        std::lock_guard<std::mutex> lock(lock_);
        CleanOutPreviousClosures();
        auto deferred_action = fit::defer_callback([this, to_run = std::move(to_run)]() mutable {
          zx_status_t result = async::PostTask(shared_fidl_dispatcher_, std::move(to_run));
          ZX_ASSERT(result == ZX_OK);
        });
        auto new_callback = std::make_shared<fit::deferred_callback>(std::move(deferred_action));
        // Every existing close should hold a reference to this deferred callback so it'll run when
        // they've all completed.
        for (auto& handle : previous_closes_) {
          auto locked = handle.lock();
          if (locked) {
            locked->AddClosureToReference(new_callback);
          }
        }
      });
  // If there are no valid previous_closes_s, then deferred_callback may be run at this point.
  ZX_ASSERT(result == ZX_OK);
}

std::unique_ptr<CodecAdmission> CodecAdmissionControl::TryAddCodecInternal(bool multi_instance) {
  std::lock_guard<std::mutex> lock(lock_);
  if (multi_instance) {
    if (single_instance_codec_count_ > 0) {
      printf(
          "CodecAdmissionControl::AddCodec(): we've already got a "
          "single-instance codec\n");
      return nullptr;
    }
    multi_instance_codec_count_++;
  } else {
    if (multi_instance_codec_count_ > 0 || single_instance_codec_count_ > 0) {
      printf(
          "CodecAdmissionControl::AddCodec(): we've already got an existing "
          "codec.  multi_instance_codec_count: %u single_instance_codec_count: "
          "%u\n",
          multi_instance_codec_count_, single_instance_codec_count_);
      return nullptr;
    }
    single_instance_codec_count_++;
  }
  // private constructor so have to explicitly new, since friending
  // std::make_unique<> would allow any class to create one.
  return std::unique_ptr<CodecAdmission>(new CodecAdmission(this, multi_instance));
}

CodecAdmissionControl::CodecAdmissionControl(async_dispatcher_t* shared_fidl_dispatcher)
    : shared_fidl_dispatcher_(shared_fidl_dispatcher),
      single_instance_codec_count_(0),
      multi_instance_codec_count_(0) {
  ZX_DEBUG_ASSERT(shared_fidl_dispatcher_);
}

void CodecAdmissionControl::RemoveCodec(bool multi_instance) {
  std::lock_guard<std::mutex> lock(lock_);
  // Else bug in caller.
  if (multi_instance) {
    ZX_DEBUG_ASSERT(multi_instance_codec_count_ > 0);
    multi_instance_codec_count_--;
  } else {
    ZX_DEBUG_ASSERT(single_instance_codec_count_ == 1);
    single_instance_codec_count_--;
  }
}

std::shared_ptr<CodecAdmissionControl::PreviousCloseHandle>
CodecAdmissionControl::OnCodecIsClosing() {
  std::lock_guard<std::mutex> lock(lock_);
  CleanOutPreviousClosures();
  auto new_close_handle = std::make_shared<PreviousCloseHandle>();
  previous_closes_.push_back(new_close_handle);
  return new_close_handle;
}

void CodecAdmissionControl::CleanOutPreviousClosures() {
  previous_closes_.erase(std::remove_if(previous_closes_.begin(), previous_closes_.end(),
                                        [](auto& ref) { return ref.expired(); }),
                         previous_closes_.end());
}

CodecAdmission::~CodecAdmission() {
  codec_admission_control_->RemoveCodec(multi_instance_);
  codec_admission_control_->codecs_to_check_for_close_.erase(this);
}

CodecAdmission::CodecAdmission(CodecAdmissionControl* codec_admission_control, bool multi_instance)
    : codec_admission_control_(codec_admission_control), multi_instance_(multi_instance) {
  ZX_DEBUG_ASSERT(codec_admission_control_);
}

void CodecAdmission::CheckIfChannelClosing() {
  if (close_handle_)
    return;
  ZX_DEBUG_ASSERT(*channel_);

  // This is only safe to call from the shared fidl thread, because otherwise this syscall could
  // race with the fidl::Binding's peer-closed detection, which closes the handle.
  zx_status_t status =
      channel_->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), nullptr);
  if (status == ZX_OK) {
    SetCodecIsClosing();
  }
}
