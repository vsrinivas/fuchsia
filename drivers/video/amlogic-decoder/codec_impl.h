// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

class DeviceCtx;

class CodecImpl : public fuchsia::mediacodec::Codec {
 public:
  CodecImpl(DeviceCtx* device,
            fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
            fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request);
  ~CodecImpl();

  // Caller should call this before Bind(), to ensure DeviceFidl is ready to
  // handle errors before they happen.
  void SetErrorHandler(fit::closure error_handler);

  // Once DeviceFidl is ready to handle errors, this enables serving Codec.
  void Bind();

  //
  // Codec interface
  //
  void EnableOnStreamFailed() override;
  void SetInputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings input_settings) override;
  void AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void SetOutputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings output_settings) override;
  void AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void FlushEndOfStreamAndCloseStream(
      uint64_t stream_lifetime_ordinal) override;
  void CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                          bool release_input_buffers,
                          bool release_output_buffers) override;
  void Sync(SyncCallback callback) override;
  void RecycleOutputPacket(
      fuchsia::mediacodec::CodecPacketHeader available_output_packet) override;
  void QueueInputFormatDetails(
      uint64_t stream_lifetime_ordinal,
      fuchsia::mediacodec::CodecFormatDetails format_details) override;
  void QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) override;
  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) override;

 private:
  DeviceCtx* device_ = nullptr;

  // TODO(dustingreen): Use or remove - probably useful to have these around but
  // we'll see.
  fuchsia::mediacodec::CreateDecoder_Params video_decoder_params_;

  // Held here temporarily until DeviceFidl is ready to handle errors so we can
  // bind.
  fidl::InterfaceRequest<fuchsia::mediacodec::Codec> tmp_interface_request_;

  // This binding doesn't channel-own this CodecImpl.  The DeviceFidl owns all
  // the CodecImpl(s).  The DeviceFidl will SetErrorHandler() such that its
  // ownership drops if the channel fails.
  fidl::Binding<fuchsia::mediacodec::Codec, CodecImpl*> binding_;
  bool is_error_handler_set_ = false;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_
