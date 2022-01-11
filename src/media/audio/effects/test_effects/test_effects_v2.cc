// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/effects/test_effects/test_effects_v2.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <optional>
#include <set>
#include <string>

using ASF = fuchsia_mediastreams::wire::AudioSampleFormat;

namespace media::audio {
namespace {

zx::vmo CreateVmoOrDie(uint64_t size_bytes) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(size_bytes, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to create VMO with size " << size_bytes;
  }
  return vmo;
}

zx::vmo DupVmoOrDie(const zx::vmo& vmo, zx_rights_t rights) {
  zx::vmo dup;
  if (auto status = vmo.duplicate(rights, &dup); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to duplicate VMO with rights 0x" << std::hex << rights;
  }
  return dup;
}

}  // namespace

// Simple FIDL server that wraps a user-defined process function.
class TestEffectsV2::TestProcessor : public fidl::WireServer<fuchsia_audio_effects::Processor> {
 public:
  TestProcessor(TestEffectsV2::ProcessFn process, zx::vmo vmo, size_t vmo_size_bytes,
                size_t output_offset_bytes,
                fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                async_dispatcher_t* dispatcher)
      : process_(process),
        binding_(fidl::BindServer(dispatcher, std::move(server_end), this,
                                  [](TestProcessor* impl, fidl::UnbindInfo info,
                                     fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end) {
                                    if (!info.is_user_initiated() && !info.is_peer_closed()) {
                                      FX_PLOGS(ERROR, info.status())
                                          << "Client disconnected unexpectedly: " << info;
                                    }
                                  })) {
    // This should not fail.
    if (auto status = mapper_.Map(vmo, 0, vmo_size_bytes); status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to map buffer with size = " << vmo_size_bytes;
    }

    input_ = reinterpret_cast<float*>(reinterpret_cast<char*>(mapper_.start()));
    output_ =
        reinterpret_cast<float*>(reinterpret_cast<char*>(mapper_.start()) + output_offset_bytes);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    float db = 0;
    if (request->options.has_total_applied_gain_db_per_input() &&
        request->options.total_applied_gain_db_per_input().count() == 1) {
      db = request->options.total_applied_gain_db_per_input()[0];
    }

    std::vector<fuchsia_audio_effects::wire::ProcessMetrics> metrics;
    if (auto status = process_(request->num_frames, input_, output_, db, metrics);
        status == ZX_OK) {
      completer.ReplySuccess(
          fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>::FromExternal(metrics));
    } else {
      completer.ReplyError(status);
    }
  }

 private:
  ProcessFn process_;
  fidl::ServerBindingRef<fuchsia_audio_effects::Processor> binding_;
  fzl::VmoMapper mapper_;
  float* input_ = nullptr;
  float* output_ = nullptr;
};

TestEffectsV2::TestEffectsV2() { loop_.StartThread("test_effects_server_v2"); }

// Must be defined in this file because TestProcessor is defined in this file.
TestEffectsV2::~TestEffectsV2() = default;

zx_status_t TestEffectsV2::AddEffect(TestEffectsV2::Effect effect) {
  FX_CHECK(effect.frames_per_second > 0);
  FX_CHECK(effect.input_channels > 0);
  FX_CHECK(effect.output_channels > 0);

  std::string name(effect.name);
  if (effects_.count(name) > 0) {
    FX_LOGS(ERROR) << "effect already added: " << name;
    return ZX_ERR_ALREADY_EXISTS;
  }
  effects_[name] = effect;
  return ZX_OK;
}

zx_status_t TestEffectsV2::ClearEffects() {
  effects_.clear();
  return ZX_OK;
}

fidl::ClientEnd<fuchsia_audio_effects::ProcessorCreator> TestEffectsV2::NewClient() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_audio_effects::ProcessorCreator>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "failed to construct a zx::channel";
  }

  HandleRequest(std::move(endpoints->server));
  return std::move(endpoints->client);
}

void TestEffectsV2::HandleRequest(
    fidl::ServerEnd<fuchsia_audio_effects::ProcessorCreator> server_end) {
  bindings_.emplace_back(fidl::BindServer(
      loop_.dispatcher(), std::move(server_end), this,
      [](TestEffectsV2* impl, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_audio_effects::ProcessorCreator> server_end) {
        if (!info.is_user_initiated() && !info.is_peer_closed()) {
          FX_PLOGS(ERROR, info.status()) << "Client disconnected unexpectedly: " << info;
        }
      }));
}

void TestEffectsV2::Create(CreateRequestView request, CreateCompleter::Sync& completer) {
  std::string name(request->name.get());
  if (effects_.count(name) == 0) {
    FX_LOGS(ERROR) << "effect not found: " << name;
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  // Translate to a ProcessorConfiguration.
  auto& effect = effects_[name];
  fidl::Arena arena;
  fuchsia_audio_effects::wire::ProcessorConfiguration config(arena);

  if (effect.max_frames_per_call > 0) {
    config.set_max_frames_per_call(arena, effect.max_frames_per_call);
  }
  if (effect.block_size_frames > 0) {
    config.set_block_size_frames(arena, effect.block_size_frames);
  }

  fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration> inputs(arena, 1);
  inputs[0].Allocate(arena);
  inputs[0].set_buffer(arena);
  inputs[0].set_format(arena);
  inputs[0].format().sample_format = ASF::kFloat;
  inputs[0].format().channel_count = effect.input_channels;
  inputs[0].format().frames_per_second = effect.frames_per_second;
  inputs[0].format().channel_layout.set_placeholder(0);

  fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration> outputs(arena, 1);
  outputs[0].Allocate(arena);
  outputs[0].set_buffer(arena);
  outputs[0].set_format(arena);
  outputs[0].format().sample_format = ASF::kFloat;
  outputs[0].format().channel_count = effect.output_channels;
  outputs[0].format().frames_per_second = effect.frames_per_second;
  outputs[0].format().channel_layout.set_placeholder(0);
  outputs[0].set_latency_frames(arena, effect.latency_frames);
  outputs[0].set_ring_out_frames(arena, effect.ring_out_frames);

  // Allocate buffers.
  // If not using in-place processing, put the buffers side-by-side in the same VMO.
  auto& input_buffer = inputs[0].buffer();
  auto& output_buffer = outputs[0].buffer();

  size_t buffer_size_frames = effect.max_frames_per_call > 0 ? effect.max_frames_per_call : 256;
  input_buffer.size = buffer_size_frames * effect.input_channels * sizeof(float);
  output_buffer.size = buffer_size_frames * effect.output_channels * sizeof(float);

  size_t buffer_size_bytes;
  if (effect.process_in_place) {
    FX_CHECK(effect.input_channels == effect.output_channels);
    buffer_size_bytes = input_buffer.size;
  } else {
    buffer_size_bytes = input_buffer.size + output_buffer.size;
    output_buffer.offset = input_buffer.size;
  }

  auto vmo = CreateVmoOrDie(buffer_size_bytes);
  input_buffer.vmo = DupVmoOrDie(vmo, ZX_RIGHT_SAME_RIGHTS);
  output_buffer.vmo = DupVmoOrDie(vmo, ZX_RIGHT_SAME_RIGHTS);

  config.set_inputs(fidl::ObjectView{arena, inputs});
  config.set_outputs(fidl::ObjectView{arena, outputs});

  // Spawn a server to implement this processor.
  zx::channel local;
  zx::channel remote;
  if (auto status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to construct a zx::channel";
  }

  auto server_end = fidl::ServerEnd<fuchsia_audio_effects::Processor>{std::move(local)};
  auto client_end = fidl::ClientEnd<fuchsia_audio_effects::Processor>{std::move(remote)};

  config.set_processor(std::move(client_end));
  processors_.insert(std::make_unique<TestProcessor>(effect.process, std::move(vmo),
                                                     buffer_size_bytes, output_buffer.offset,
                                                     std::move(server_end), loop_.dispatcher()));

  completer.ReplySuccess(std::move(config));
}

}  // namespace media::audio
