// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_impl.h"

#include "device_ctx.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fxl/logging.h>

#include <threads.h>

CodecImpl::CodecImpl(
    DeviceCtx* device,
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request)
    : device_(device),
      video_decoder_params_(std::move(video_decoder_params)),
      tmp_interface_request_(std::move(codec_request)),
      binding_(this) {
  FXL_DCHECK(tmp_interface_request_);
}

CodecImpl::~CodecImpl() {
  // We need ~binding_ to run on shared_fidl_thread() else it's not safe to
  // un-bind unilaterally.  Unless not bound in the first place.
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread() ||
             !binding_.is_bound());

  // ~binding_ here + fact that we're running on shared_fidl_thread() (if Bind()
  // previously called) means error_handler won't be running concurrently with
  // ~CodecImpl and won't run after ~binding_ here.
}

void CodecImpl::SetErrorHandler(fit::closure error_handler) {
  FXL_DCHECK(!binding_.is_bound());
  binding_.set_error_handler([this, error_handler = std::move(error_handler)] {
    FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
    error_handler();
  });
  is_error_handler_set_ = true;
}

void CodecImpl::Bind() {
  // While it would potentially be safe to call Bind() from a thread other than
  // shared_fidl_thread(), we have no reason to permit that.
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
  FXL_DCHECK(is_error_handler_set_);
  FXL_DCHECK(!binding_.is_bound());
  FXL_DCHECK(tmp_interface_request_);
  // Go!  (immediately - if Bind() is called on IOCTL thread, this can result in
  // _immediate_ dispatching over on shared_fidl_thread()).
  binding_.Bind(std::move(tmp_interface_request_),
                device_->driver()->shared_fidl_loop()->dispatcher());
  FXL_DCHECK(!tmp_interface_request_);
}

void CodecImpl::EnableOnStreamFailed() {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::SetInputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings input_settings) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::SetOutputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings output_settings) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::FlushEndOfStreamAndCloseStream(
    uint64_t stream_lifetime_ordinal) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                                   bool release_input_buffers,
                                   bool release_output_buffers) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::Sync(SyncCallback callback) {
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
  // TODO(dustingreen): Ensure the real implementation satisfies the
  // StreamControl ordering domain semantics.  The current implementation is
  // just to verify that a CodecImpl can be bound to a channel and communicate
  // on that channel in both directions.
  callback();
}

void CodecImpl::RecycleOutputPacket(
    fuchsia::mediacodec::CodecPacketHeader available_output_packet) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::QueueInputFormatDetails(
    uint64_t stream_lifetime_ordinal,
    fuchsia::mediacodec::CodecFormatDetails format_details) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void CodecImpl::QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) {
  FXL_CHECK(false) << "not yet implemented";
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}
