// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame_encoder.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/logging.h>

#include <thread>

namespace {

// This code only has one stream_lifetime_ordinal which is 1.
constexpr uint64_t kStreamLifetimeOrdinal = 1;

std::vector<FrameEncoder::EncodedFrame> take_encoded_frames_from_codec(
    CodecClient* client, bool expect_access_units) {
  std::vector<FrameEncoder::EncodedFrame> frames;
  std::unique_ptr<CodecOutput> output;
  FXL_VLOG(6) << "Waiting on output packets...";
  while ((output = client->BlockingGetEmittedOutput()) &&
         !output->end_of_stream()) {
    const auto& packet = output->packet();
    const auto& buffer = client->GetOutputBufferByIndex(packet.buffer_index());

    if (expect_access_units) {
      FXL_CHECK(packet.start_access_unit());
      FXL_CHECK(packet.known_end_access_unit());
    }

    FXL_VLOG(8) << "Got output packet with length: "
                << packet.valid_length_bytes();

    std::vector<uint8_t> payload;
    payload.resize(packet.valid_length_bytes());

    memcpy(payload.data(), buffer.base() + packet.start_offset(),
           packet.valid_length_bytes());

    std::optional<uint64_t> timestamp_ish;
    if (packet.has_timestamp_ish()) {
      FXL_VLOG(8) << "Output packet has timestamp: " << packet.timestamp_ish();
      timestamp_ish = packet.timestamp_ish();
    }

    frames.push_back({
        .data = std::move(payload),
        .timestamp_ish = timestamp_ish,
    });

    client->RecycleOutputPacket(fidl::Clone(packet.header()));
  }
  FXL_VLOG(3) << "Encoder returned EOS.";

  return frames;
}

void feed_raw_frames_into_codec(const FrameEncoder::Payload& payload,
                                CodecClient* client) {
  auto payload_len = [&payload](size_t frame_index) -> size_t {
    if (frame_index >= payload.offsets.size()) {
      return 0;
    }

    if (frame_index + 1 == payload.offsets.size()) {
      return payload.data.size() - payload.offsets[frame_index].position;
    }

    return payload.offsets[frame_index + 1].position -
           payload.offsets[frame_index].position;
  };

  size_t frame = 0;
  size_t len = 0;
  while ((len = payload_len(frame)) > 0) {
    FXL_VLOG(10) << "Waiting on input packet.";
    auto packet = client->BlockingGetFreeInputPacket();
    FXL_VLOG(10) << "Got input packet: " << packet.get();
    FXL_CHECK(packet);

    const auto& buffer = client->GetInputBufferByIndex(packet->buffer_index());
    auto* packet_start = &payload.data[payload.offsets[frame].position];
    auto maybe_timestamp_ish = payload.offsets[frame++].timestamp_ish;

    packet->set_stream_lifetime_ordinal(kStreamLifetimeOrdinal);
    packet->set_start_offset(0);
    packet->set_valid_length_bytes(len);
    if (maybe_timestamp_ish) {
      packet->set_timestamp_ish(*maybe_timestamp_ish);
    }
    memcpy(buffer.base(), packet_start, len);
    client->QueueInputPacket(std::move(packet));
  }

  client->QueueInputEndOfStream(kStreamLifetimeOrdinal);
  FXL_VLOG(3) << "Finished sending frames and EOS to encoder.";
}  // namespace

void ConnectToCodec(
    fidl::InterfaceRequest<fuchsia::media::StreamProcessor> request,
    const fuchsia::media::FormatDetails& format,
    component::StartupContext* startup_context) {
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "codec_factory failed - unexpected; status: " << status;
  });

  startup_context
      ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>(
          codec_factory.NewRequest());

  FXL_VLOG(3) << "Connected to CodecFactory service.";

  fuchsia::mediacodec::CreateEncoder_Params params;
  params.set_input_details(fidl::Clone(format));
  codec_factory->CreateEncoder(std::move(params), std::move(request));
  FXL_VLOG(3) << "Requested encoder from factory.";
}

}  // namespace

// static
std::vector<FrameEncoder::EncodedFrame> FrameEncoder::EncodeFrames(
    const Payload& payload, const fuchsia::media::FormatDetails& format,
    component::StartupContext* startup_context, bool expect_access_units) {
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
  startup_context->ConnectToEnvironmentService<fuchsia::sysmem::Allocator>(
      sysmem.NewRequest());

  async::Loop fidl_loop(&kAsyncLoopConfigNoAttachToThread);
  auto client = std::make_unique<CodecClient>(&fidl_loop, std::move(sysmem));
  fidl_loop.StartThread("FrameEncoder_fidl");

  ConnectToCodec(client->GetTheRequestOnce(), format, startup_context);

  fuchsia::media::FormatDetails encoded_format_details;
  std::vector<EncodedFrame> encoded_frames;

  client->Start();
  auto consumer_thread = std::make_unique<std::thread>(
      [&fidl_loop, &encoded_format_details, &encoded_frames,
       expect_access_units, client = client.get()] {
        FXL_VLOG(3) << "Starting to receive frames from codec...";
        encoded_frames =
            take_encoded_frames_from_codec(client, expect_access_units);
        async::PostTask(fidl_loop.dispatcher(),
                        [&fidl_loop] { fidl_loop.Quit(); });
      });

  feed_raw_frames_into_codec(payload, client.get());
  consumer_thread->join();
  fidl_loop.JoinThreads();

  return encoded_frames;
}
