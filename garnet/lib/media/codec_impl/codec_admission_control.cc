// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_admission_control.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <mutex>

void CodecAdmissionControl::TryAddCodec(
    bool multi_instance,
    fit::function<void(std::unique_ptr<CodecAdmission>)>
        continue_after_previously_started_channel_closes_done) {
  PostAfterPreviouslyStartedClosesDone(
      [this, multi_instance,
       callback =
           std::move(continue_after_previously_started_channel_closes_done)] {
        callback(TryAddCodecInternal(multi_instance));
      });
}

void CodecAdmissionControl::PostAfterPreviouslyStartedClosesDone(
    fit::closure to_run) {
  // TODO(dustingreen): This post is a partial simulation of more robust fencing
  // of previously initiated closes before newly initiated create.  See TODO in
  // the header file.
  zx_status_t result =
      async::PostTask(shared_fidl_dispatcher_, std::move(to_run));
  ZX_ASSERT(result == ZX_OK);
}

std::unique_ptr<CodecAdmission> CodecAdmissionControl::TryAddCodecInternal(
    bool multi_instance) {
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
  return std::unique_ptr<CodecAdmission>(
      new CodecAdmission(this, multi_instance));
}

CodecAdmissionControl::CodecAdmissionControl(
    async_dispatcher_t* shared_fidl_dispatcher)
    : shared_fidl_dispatcher_(shared_fidl_dispatcher) {
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

CodecAdmission::~CodecAdmission() {
  codec_admission_control_->RemoveCodec(multi_instance_);
}

CodecAdmission::CodecAdmission(CodecAdmissionControl* codec_admission_control,
                               bool multi_instance)
    : codec_admission_control_(codec_admission_control),
      multi_instance_(multi_instance) {
  ZX_DEBUG_ASSERT(codec_admission_control_);
}
