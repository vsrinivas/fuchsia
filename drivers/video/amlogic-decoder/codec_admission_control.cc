// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_admission_control.h"

#include "device_ctx.h"

#include <lib/fxl/logging.h>

#include <stdint.h>
#include <mutex>

void CodecAdmissionControl::TryAddCodec(
    fit::function<void(std::unique_ptr<CodecAdmission>)>
        continue_after_previously_started_channel_closes_done) {
  PostAfterPreviouslyStartedClosesDone(
      [this, callback = std::move(
                 continue_after_previously_started_channel_closes_done)] {
        callback(TryAddCodecInternal());
      });
}

void CodecAdmissionControl::PostAfterPreviouslyStartedClosesDone(
    fit::closure to_run) {
  // TODO(dustingreen): This post is a partial simulation of more robust fencing
  // of previously initiated closes before newly initiated create.  See TODO in
  // the header file.
  device_ctx_->driver()->PostToSharedFidl(std::move(to_run));
}

std::unique_ptr<CodecAdmission> CodecAdmissionControl::TryAddCodecInternal() {
  std::lock_guard<std::mutex> lock(lock_);
  if (codec_count_ != 0) {
    FXL_DCHECK(codec_count_ == 1);
    printf("CodecAdmissionControl::AddCodec(): we've already got one\n");
    return nullptr;
  }
  codec_count_++;
  FXL_DCHECK(codec_count_ == 1);
  // private constructor so have to explicitly new, since friending
  // std::make_unique<> would allow any class to create one.
  return std::unique_ptr<CodecAdmission>(new CodecAdmission(this));
}

CodecAdmissionControl::CodecAdmissionControl(DeviceCtx* device_ctx)
    : device_ctx_(device_ctx) {
  FXL_DCHECK(device_ctx_);
}

void CodecAdmissionControl::RemoveCodec() {
  std::lock_guard<std::mutex> lock(lock_);
  // Else bug in caller.
  FXL_DCHECK(codec_count_ == 1);
  codec_count_--;
}

CodecAdmission::~CodecAdmission() { codec_admission_control_->RemoveCodec(); }

CodecAdmission::CodecAdmission(CodecAdmissionControl* codec_admission_control)
    : codec_admission_control_(codec_admission_control) {
  FXL_DCHECK(codec_admission_control_);
}
