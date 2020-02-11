// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-topology.h"

#include <zircon/device/audio.h>

#include <memory>

#include <fbl/string_printf.h>

#include "intel-dsp-ipc.h"
#include "intel-dsp-modules.h"
#include "intel-dsp.h"

namespace audio {
namespace intel_hda {

namespace {
// Module config parameters extracted from kbl_i2s_chrome.conf

// To route audio from the system memory to the audio codecs, we must
// set up an appropriate _topology_ inside the DSP. Topologies consist
// of _pipelines_ and _modules_.
//
// Each module performs some operation on the audio, such as copying it
// to/from a DMA gateway; mixing the output of other modules together;
// modifying the volume of the stream; etc. Each module is given
// a unique name of the form (<module type>, <id>). For example,
// (<COPIER>, 0), (<COPIER>, 1) and (<DEMUX>, 0) are three unique names.
//
// Pipelines are used to instruct the DSP how to schedule modules. Every
// module needs to be inside a pipeline. Each pipeline can have an
// arbitrary number of modules, with the following constraints:
//
//   * If a module connects to another module in the same pipeline, it must
//     use output pin 0.
//
//   * A pipeline can only have a single linear series of modules (i.e., no
//     forking within the pipeline, but forking to another pipeline is permitted).
//
// Currently, the only type of module we use in our topology is
// a _Copier_ module. Copiers are a type of module which may be
// configured to copy audio data from:
//
//   * A DMA gateway to another module
//   * a module to another module
//   * a module to a DMA gateway
//
// but cannot copy directly from DMA to DMA.
//
// We currently set up a default topology consisting of two pipelines,
// as follows:
//
//    Playback: [host DMA gateway] -> copier -> copier -> [I2S gateway]
//    Capture:  [I2S gateway] -> copier -> copier -> [host DMA gateway]

constexpr uint8_t I2S0_BUS = 0;
constexpr uint8_t I2S1_BUS = 1;

// Use 48khz 16-bit stereo for host input/output.
constexpr AudioDataFormat kHostFormat = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_16BIT,
    .channel_map = 0xFFFFFF10,
    .channel_config = ChannelConfig::CONFIG_STEREO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 2,
    .valid_bit_depth = 16,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};

// Format used by the Eve's Max98927 speaker codecs, and onboard mic,
// which are both on the I2S-0 bus.
constexpr AudioDataFormat kFormatI2S0Bus = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_32BIT,
    .channel_map = 0xFFFFFF10,
    .channel_config = ChannelConfig::CONFIG_STEREO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 2,
    .valid_bit_depth = 16,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};
constexpr AudioDataFormat kFormatMax98927 = kFormatI2S0Bus;
constexpr AudioDataFormat kFormatDmic = kFormatI2S0Bus;

// Format used by the Eve's ALC5663 headphone codec.
constexpr AudioDataFormat kFormatAlc5663 = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_32BIT,
    .channel_map = 0xFFFFFF10,
    .channel_config = ChannelConfig::CONFIG_STEREO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 2,
    .valid_bit_depth = 24,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};

// Format used for intermediate DSP operations.
const AudioDataFormat kDspFormat = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_32BIT,
    .channel_map = 0xFFFFFF10,
    .channel_config = ChannelConfig::CONFIG_STEREO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 2,
    .valid_bit_depth = 32,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};

constexpr uint32_t AudioBytesPerSec(const AudioDataFormat& format) {
  return static_cast<uint32_t>(format.sampling_frequency) *
         (static_cast<uint32_t>(format.bit_depth) / 8) * format.number_of_channels;
}

CopierCfg CreateCopierCfg(AudioDataFormat input, AudioDataFormat output) {
  CopierCfg result = {};

  // Setup input/output formats.
  result.base_cfg.audio_fmt = input;
  result.out_fmt = output;

  // Calculate input and output buffer sizes. The copier needs 1ms of data.
  result.base_cfg.ibs = AudioBytesPerSec(input) / 1000;
  result.base_cfg.obs = AudioBytesPerSec(output) / 1000;

  // Set cycles per input frame to 100k (arbitrary).
  result.base_cfg.cpc = 100'000;

  return result;
}

CopierCfg CreateGatewayCopierCfg(const AudioDataFormat& input, const AudioDataFormat& output,
                                 uint32_t gateway_node_id) {
  // Create base config.
  CopierCfg result = CreateCopierCfg(input, output);
  result.gtw_cfg.node_id = gateway_node_id;

  // Set the DMA buffer size to 2 times the input/output frame size.
  result.gtw_cfg.dma_buffer_size = std::max(result.base_cfg.ibs, result.base_cfg.obs) * 2;

  return result;
}

// Copy the given ranges of bytes into a new std::vector<uint8_t>.
std::vector<uint8_t> RawBytesOf(const uint8_t* object, size_t size) {
  std::vector<uint8_t> result;
  result.resize(size);
  memcpy(result.data(), object, size);
  return result;
}

// Copy the underlying bytes of the given object to a new std::vector<uint8_t>.
template <typename T>
std::vector<uint8_t> RawBytesOf(const T* object) {
  return RawBytesOf(reinterpret_cast<const uint8_t*>(object), sizeof(*object));
}

zx_status_t GetI2SBlob(const Nhlt& nhlt, uint8_t bus_id, uint8_t direction,
                       const AudioDataFormat& format, const void** out_blob, size_t* out_size) {
  for (const auto& cfg : nhlt.i2s_configs()) {
    if ((cfg.bus_id != bus_id) || (cfg.direction != direction)) {
      continue;
    }
    // TODO better matching here
    for (const I2SConfig::Format& endpoint_format : cfg.formats) {
      if (format.valid_bit_depth != endpoint_format.config.valid_bits_per_sample) {
        continue;
      }
      *out_blob = endpoint_format.capabilities.data();
      *out_size = endpoint_format.capabilities.size();
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

StatusOr<std::vector<uint8_t>> GetI2SModuleConfig(const Nhlt& nhlt, uint8_t i2s_instance_id,
                                                  uint8_t direction, const CopierCfg& base_cfg) {
  const void* blob;
  size_t blob_size;
  zx_status_t st = GetI2SBlob(
      nhlt, i2s_instance_id, direction,
      (direction == NHLT_DIRECTION_RENDER) ? base_cfg.out_fmt : base_cfg.base_cfg.audio_fmt, &blob,
      &blob_size);
  if (st != ZX_OK) {
    return Status(st);
  }

  // Copy the I2S config blob
  size_t cfg_size = sizeof(base_cfg) + blob_size;
  ZX_DEBUG_ASSERT(cfg_size <= UINT16_MAX);

  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> cfg_buf(new (&ac) uint8_t[cfg_size]);
  if (!ac.check()) {
    return Status(ZX_ERR_NO_MEMORY,
                  "out of memory while attempting to allocate copier config buffer");
  }
  memcpy(cfg_buf.get() + sizeof(base_cfg), blob, blob_size);

  // Copy the copier config
  memcpy(cfg_buf.get(), &base_cfg, sizeof(base_cfg));
  auto copier_cfg = reinterpret_cast<CopierCfg*>(cfg_buf.get());
  copier_cfg->gtw_cfg.config_length = static_cast<uint32_t>(blob_size);

  return RawBytesOf(cfg_buf.get(), cfg_size);
}

// Create a pieline transferring data from the host to an I2S bus.
//
// The I2S device must be present in the given NHLT table.
StatusOr<DspPipelineId> ConnectHostToI2S(const Nhlt& nhlt, DspModuleController* controller,
                                         uint16_t copier_module_id, uint32_t host_gateway_id,
                                         uint32_t i2s_gateway_id, uint8_t i2s_bus,
                                         const AudioDataFormat& i2s_format) {
  CopierCfg host_out_copier = CreateGatewayCopierCfg(kHostFormat, kDspFormat, host_gateway_id);
  CopierCfg i2s_out_copier = CreateGatewayCopierCfg(kDspFormat, i2s_format, i2s_gateway_id);
  StatusOr<std::vector<uint8_t>> i2s_out_gateway_cfg =
      GetI2SModuleConfig(nhlt, i2s_bus, NHLT_DIRECTION_RENDER, i2s_out_copier);
  if (!i2s_out_gateway_cfg.ok()) {
    return i2s_out_gateway_cfg.status();
  }

  return CreateSimplePipeline(controller,
                              {
                                  // Copy from host DMA.
                                  {copier_module_id, RawBytesOf(&host_out_copier)},
                                  // Copy to I2S.
                                  {copier_module_id, i2s_out_gateway_cfg.ConsumeValueOrDie()},
                              });
}

// Create a pieline transferring data from the I2S bus to the host.
//
// The I2S device must be present in the given NHLT table.
StatusOr<DspPipelineId> ConnectI2SToHost(const Nhlt& nhlt, DspModuleController* controller,
                                         uint16_t copier_module_id, uint32_t i2s_gateway_id,
                                         uint8_t i2s_bus, uint32_t host_gateway_id,
                                         const AudioDataFormat& i2s_format) {
  CopierCfg i2s_in_copier = CreateGatewayCopierCfg(i2s_format, kDspFormat, i2s_gateway_id);
  CopierCfg host_in_copier = CreateGatewayCopierCfg(kDspFormat, kHostFormat, host_gateway_id);
  StatusOr<std::vector<uint8_t>> i2s_in_gateway_cfg =
      GetI2SModuleConfig(nhlt, i2s_bus, NHLT_DIRECTION_CAPTURE, i2s_in_copier);
  if (!i2s_in_gateway_cfg.ok()) {
    return i2s_in_gateway_cfg.status();
  }

  return CreateSimplePipeline(controller,
                              {
                                  // Copy from I2S.
                                  {copier_module_id, i2s_in_gateway_cfg.ConsumeValueOrDie()},
                                  // Copy to host DMA.
                                  {copier_module_id, RawBytesOf(&host_in_copier)},
                              });
}

// Get the module ID corresponding of the given module name.
StatusOr<uint16_t> GetModuleId(DspModuleController* controller, const char* name) {
  // Read available modules.
  StatusOr<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> modules_or_err =
      controller->ReadModuleDetails();
  if (!modules_or_err.ok()) {
    return modules_or_err.status();
  }
  auto& modules = modules_or_err.ValueOrDie();

  // Fetch out the copier module.
  auto copier_it = modules.find(name);
  if (copier_it == modules.end()) {
    return Status(ZX_ERR_NOT_FOUND,
                  fbl::StringPrintf("DSP doesn't have support for module '%s'", name));
  }
  return copier_it->second->module_id;
}

// Set up the DSP to handle the Pixelbook Eve's topology.
struct PixelbookEvePipelines {
  DspPipelineId speakers;
  DspPipelineId inbuilt_microphone;
  DspPipelineId headphone;
};
StatusOr<PixelbookEvePipelines> SetUpPixelbookEvePipelines(const Nhlt& nhlt,
                                                           DspModuleController* controller) {
  // Get the ID of the "COPIER" module.
  StatusOr<uint16_t> copier_module_id_or_err = GetModuleId(controller, "COPIER");
  if (!copier_module_id_or_err.ok()) {
    return copier_module_id_or_err.status();
  }
  uint16_t copier_module_id = copier_module_id_or_err.ValueOrDie();

  // Create output pipeline to MAX98927 codec.
  StatusOr<DspPipelineId> speakers = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 0),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S0_BUS, 0), I2S0_BUS, kFormatMax98927);
  if (!speakers.ok()) {
    return PrependMessage("Could not set up route to MAX98927 codec", speakers.status());
  }

  // Create output pipeline to ALC5663 codec.
  StatusOr<DspPipelineId> headphones = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 1),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S1_BUS, 0), I2S1_BUS, kFormatAlc5663);
  if (!headphones.ok()) {
    return PrependMessage("Could not set up route to ALC5663 codec", headphones.status());
  }

  // Create input pipeline from DMIC.
  StatusOr<DspPipelineId> inbuilt_microphone =
      ConnectI2SToHost(nhlt, controller, copier_module_id,
                       I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_INPUT, I2S0_BUS, 0), I2S0_BUS,
                       HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 0), kFormatDmic);
  if (!inbuilt_microphone.ok()) {
    return PrependMessage("Could not set up route from DMIC", inbuilt_microphone.status());
  }

  PixelbookEvePipelines result = {};
  result.inbuilt_microphone = inbuilt_microphone.ValueOrDie();
  result.speakers = speakers.ValueOrDie();
  result.headphone = headphones.ValueOrDie();
  return result;
}

}  // namespace

Status IntelDsp::StartPipeline(DspPipeline pipeline) {
  // Pipeline must be paused before starting.
  if (Status status =
          module_controller_->SetPipelineState(pipeline.id, PipelineState::PAUSED, true);
      !status.ok()) {
    return status;
  }

  // Start the pipeline.
  if (Status status =
          module_controller_->SetPipelineState(pipeline.id, PipelineState::RUNNING, true);
      !status.ok()) {
    return status;
  }

  return OkStatus();
}

Status IntelDsp::PausePipeline(DspPipeline pipeline) {
  if (Status status =
          module_controller_->SetPipelineState(pipeline.id, PipelineState::PAUSED, true);
      !status.ok()) {
    return status;
  }

  if (Status status = module_controller_->SetPipelineState(pipeline.id, PipelineState::RESET, true);
      !status.ok()) {
    return status;
  }

  return OkStatus();
}

zx_status_t IntelDsp::CreateAndStartStreams() {
  zx_status_t res = ZX_OK;

  // Setup the pipelines.
  StatusOr<PixelbookEvePipelines> pipelines =
      SetUpPixelbookEvePipelines(*nhlt_, module_controller_.get());
  if (!pipelines.ok()) {
    LOG(ERROR, "Failed to set up DSP pipelines: %s\n", pipelines.status().ToString().c_str());
    return pipelines.status().code();
  }

  // Create and publish the streams we will use.
  static struct {
    uint32_t stream_id;
    bool is_input;
    DspPipeline pipeline;
    audio_stream_unique_id_t uid;
    fbl::String name;
  } STREAMS[] = {
      // Speakers
      {
          .stream_id = 1,
          .is_input = false,
          .pipeline = {pipelines.ValueOrDie().speakers},
          .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS,
          .name = "Builtin Speakers",
      },
      // DMIC
      {
          .stream_id = 2,
          .is_input = true,
          .pipeline = {pipelines.ValueOrDie().inbuilt_microphone},
          .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE,
          .name = "Builtin Microphone",
      },
      // Headphones
      {
          .stream_id = 3,
          .is_input = false,
          .pipeline = {pipelines.ValueOrDie().headphone},
          .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
          .name = "Builtin Headphone Jack",
      },
  };

  for (const auto& stream_def : STREAMS) {
    auto stream =
        fbl::AdoptRef(new IntelDspStream(stream_def.stream_id, stream_def.is_input,
                                         stream_def.pipeline, stream_def.name, &stream_def.uid));

    res = ActivateStream(stream);
    if (res != ZX_OK) {
      LOG(ERROR, "Failed to activate %s stream id #%u (res %d)!",
          stream_def.is_input ? "input" : "output", stream_def.stream_id, res);
      return res;
    }
  }

  return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
