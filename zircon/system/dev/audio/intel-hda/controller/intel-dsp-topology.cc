// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-topology.h"

#include <zircon/device/audio.h>

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

// Use 48khz 16-bit stereo throughout
constexpr AudioDataFormat FMT_HOST = {
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

constexpr AudioDataFormat FMT_I2S = {
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

// Mixer modules only operate on 32-bits
const AudioDataFormat FMT_MIXER = {
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

const CopierCfg HOST_OUT_COPIER_CFG = {
    .base_cfg =
        {
            .cpc = 100000,
            .ibs = 384,
            .obs = 384,
            .is_pages = 0,
            .audio_fmt = FMT_HOST,
        },
    .out_fmt = FMT_MIXER,
    .copier_feature_mask = 0,
    .gtw_cfg =
        {
            .node_id = HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 0),
            .dma_buffer_size = 2 * 384,
            .config_length = 0,
        },
};

const CopierCfg HOST_IN_COPIER_CFG = {
    .base_cfg =
        {
            .cpc = 100000,
            .ibs = 384,
            .obs = 384,
            .is_pages = 0,
            .audio_fmt = FMT_HOST,
        },
    .out_fmt = FMT_HOST,
    .copier_feature_mask = 0,
    .gtw_cfg =
        {
            .node_id = HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 0),
            .dma_buffer_size = 2 * 384,
            .config_length = 0,
        },
};

constexpr uint8_t I2S_OUT_INSTANCE_ID = 0;

const CopierCfg I2S_OUT_COPIER_CFG = {
    .base_cfg =
        {
            .cpc = 100000,
            .ibs = 384,
            .obs = 384,
            .is_pages = 0,
            .audio_fmt = FMT_MIXER,
        },
    .out_fmt = FMT_I2S,
    .copier_feature_mask = 0,
    .gtw_cfg =
        {
            .node_id = I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S_OUT_INSTANCE_ID, 0),
            .dma_buffer_size = 2 * 384,
            .config_length = 0,
        },
};

constexpr uint8_t I2S_IN_INSTANCE_ID = 0;

const CopierCfg I2S_IN_COPIER_CFG = {
    .base_cfg =
        {
            .cpc = 100000,
            .ibs = 384,
            .obs = 384,
            .is_pages = 0,
            .audio_fmt = FMT_I2S,
        },
    .out_fmt = FMT_HOST,
    .copier_feature_mask = 0,
    .gtw_cfg =
        {
            .node_id = I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_INPUT, I2S_IN_INSTANCE_ID, 0),
            .dma_buffer_size = 2 * 384,
            .config_length = 0,
        },
};

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
    return Status(st, fbl::StringPrintf("I2S config (instance %u direction %u) not found\n",
                                        i2s_instance_id, direction));
  }

  // Copy the I2S config blob
  size_t cfg_size = sizeof(base_cfg) + blob_size;
  ZX_DEBUG_ASSERT(cfg_size <= UINT16_MAX);

  fbl::AllocChecker ac;
  fbl::unique_ptr<uint8_t[]> cfg_buf(new (&ac) uint8_t[cfg_size]);
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

// Set up the DSP to connect inputs and outputs.
struct SystemPipelines {
  DspPipelineId speakers;
  DspPipelineId inbuilt_microphone;
};
StatusOr<SystemPipelines> SetupPipelines(const Nhlt& nhlt, DspModuleController* controller) {
  // Read available modules.
  StatusOr<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> modules_or_err =
      controller->ReadModuleDetails();
  if (!modules_or_err.ok()) {
    return modules_or_err.status();
  }
  auto& modules = modules_or_err.ValueOrDie();

  // Fetch out the copier module.
  auto copier_it = modules.find("COPIER");
  if (copier_it == modules.end()) {
    return Status(ZX_ERR_NOT_SUPPORTED, "DSP doesn't have support for COPIER module.");
  }
  uint16_t copier_id = copier_it->second->module_id;

  // Create output pipeline.
  StatusOr<std::vector<uint8_t>> i2s0_out_gateway_cfg =
      GetI2SModuleConfig(nhlt, I2S_OUT_INSTANCE_ID, NHLT_DIRECTION_RENDER, I2S_OUT_COPIER_CFG);
  if (!i2s0_out_gateway_cfg.ok()) {
    return i2s0_out_gateway_cfg.status();
  }
  StatusOr<DspPipelineId> playback_pipeline =
      CreateSimplePipeline(controller, {
                                           // Copy from host DMA.
                                           {copier_id, RawBytesOf(&HOST_OUT_COPIER_CFG)},
                                           // Copy to I2S.
                                           {copier_id, i2s0_out_gateway_cfg.ConsumeValueOrDie()},
                                       });
  if (!playback_pipeline.ok()) {
    return playback_pipeline.status();
  }

  // Create input pipeline.
  StatusOr<std::vector<uint8_t>> i2s0_in_gateway_cfg =
      GetI2SModuleConfig(nhlt, I2S_IN_INSTANCE_ID, NHLT_DIRECTION_CAPTURE, I2S_IN_COPIER_CFG);
  if (!i2s0_in_gateway_cfg.ok()) {
    return i2s0_in_gateway_cfg.status();
  }
  StatusOr<DspPipelineId> capture_pipeline =
      CreateSimplePipeline(controller, {
                                           // Copy from I2S.
                                           {copier_id, i2s0_in_gateway_cfg.ConsumeValueOrDie()},
                                           // Copy to host DMA.
                                           {copier_id, RawBytesOf(&HOST_IN_COPIER_CFG)},
                                       });
  if (!capture_pipeline.ok()) {
    return capture_pipeline.status();
  }

  return SystemPipelines{playback_pipeline.ValueOrDie(), capture_pipeline.ValueOrDie()};
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
  StatusOr<SystemPipelines> pipelines = SetupPipelines(*nhlt_, module_controller_.get());
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
  } STREAMS[] = {
      // Speakers
      {
          .stream_id = 1,
          .is_input = false,
          .pipeline = {pipelines.ValueOrDie().speakers},
          .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS,
      },
      // DMIC
      {
          .stream_id = 2,
          .is_input = true,
          .pipeline = {pipelines.ValueOrDie().inbuilt_microphone},
          .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE,
      },
  };

  for (const auto& stream_def : STREAMS) {
    auto stream = fbl::AdoptRef(new IntelDspStream(stream_def.stream_id, stream_def.is_input,
                                                   stream_def.pipeline, &stream_def.uid));

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
