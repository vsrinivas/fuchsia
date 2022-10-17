// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-topology.h"

#include <memory>

#include <fbl/string_printf.h>

#include "intel-dsp-ipc.h"
#include "intel-dsp-modules.h"
#include "intel-dsp.h"

namespace audio {
namespace intel_hda {

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

// Use 48khz 16-bit stereo for host I2S input/output.
constexpr AudioDataFormat kHostI2sFormat = {
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

// Format used for intermediate DSP operations in I2S input/output.
const AudioDataFormat kDspI2sFormat = {
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
// Format used for I2S0 bus input/output.
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
// Format used for I2S1 bus input/output.
constexpr AudioDataFormat kFormatI2S1Bus = {
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

zx_status_t GetNhltBlob(const Nhlt& nhlt, uint8_t bus_id, uint8_t direction, uint8_t link_type,
                        const AudioDataFormat& format, const void** out_blob, size_t* out_size) {
  for (const auto& cfg : nhlt.configs()) {
    if ((cfg.bus_id != bus_id) || (cfg.direction != direction) ||
        (cfg.header.link_type != link_type)) {
      continue;
    }
    // TODO better matching here
    for (const EndPointConfig::Format& endpoint_format : cfg.formats) {
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

zx::result<std::vector<uint8_t>> GetModuleConfig(const Nhlt& nhlt, uint8_t i2s_instance_id,
                                                 uint8_t direction, uint8_t link_type,
                                                 const CopierCfg& base_cfg) {
  const void* blob;
  size_t blob_size;
  zx_status_t st = GetNhltBlob(
      nhlt, i2s_instance_id, direction, link_type,
      (direction == NHLT_DIRECTION_RENDER) ? base_cfg.out_fmt : base_cfg.base_cfg.audio_fmt, &blob,
      &blob_size);
  if (st != ZX_OK) {
    return zx::error(st);
  }

  // Copy the I2S config blob
  size_t cfg_size = sizeof(base_cfg) + blob_size;
  ZX_DEBUG_ASSERT(cfg_size <= UINT16_MAX);

  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> cfg_buf(new (&ac) uint8_t[cfg_size]);
  if (!ac.check()) {
    GLOBAL_LOG(ERROR, "out of memory while attempting to allocate copier config buffer");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Copy the copier config.
  memcpy(cfg_buf.get(), &base_cfg, sizeof(base_cfg));
  auto copier_cfg = reinterpret_cast<CopierCfg*>(cfg_buf.get());
  ZX_ASSERT(!(blob_size % kCopierBytesPerWord));
  copier_cfg->gtw_cfg.config_words = static_cast<uint32_t>(blob_size) / kCopierBytesPerWord;

  // Copy the config data.
  size_t offset_to_data = offsetof(CopierCfg, gtw_cfg) + offsetof(CopierGatewayCfg, config_data);
  memcpy(cfg_buf.get() + offset_to_data, blob, blob_size);

  // DSP expects appending one empty word (4 bytes) to the config data.
  // Space is reserved in CopierGatewayCfg config_data.
  memset(cfg_buf.get() + offset_to_data + blob_size, 0, 4);

  return zx::ok(RawBytesOf(cfg_buf.get(), cfg_size));
}

// Create a pieline transferring data from the host to an I2S bus.
//
// The I2S device must be present in the given NHLT table.
zx::result<DspPipelineId> ConnectHostToI2S(const Nhlt& nhlt, DspModuleController* controller,
                                           uint16_t copier_module_id, uint32_t host_gateway_id,
                                           uint32_t i2s_gateway_id, uint8_t i2s_bus,
                                           const AudioDataFormat& i2s_format) {
  CopierCfg host_out_copier =
      CreateGatewayCopierCfg(kHostI2sFormat, kDspI2sFormat, host_gateway_id);
  CopierCfg i2s_out_copier = CreateGatewayCopierCfg(kDspI2sFormat, i2s_format, i2s_gateway_id);
  zx::result<std::vector<uint8_t>> i2s_out_gateway_cfg =
      GetModuleConfig(nhlt, i2s_bus, NHLT_DIRECTION_RENDER, NHLT_LINK_TYPE_SSP, i2s_out_copier);
  if (!i2s_out_gateway_cfg.is_ok()) {
    return zx::error(i2s_out_gateway_cfg.status_value());
  }

  return CreateSimplePipeline(controller, {
                                              // Copy from host DMA.
                                              {copier_module_id, RawBytesOf(&host_out_copier)},
                                              // Copy to I2S.
                                              {copier_module_id, i2s_out_gateway_cfg.value()},
                                          });
}

// Create a pieline transferring data from the I2S bus to the host.
//
// The I2S device must be present in the given NHLT table.
zx::result<DspPipelineId> ConnectI2SToHost(const Nhlt& nhlt, DspModuleController* controller,
                                           uint16_t copier_module_id, uint32_t i2s_gateway_id,
                                           uint8_t i2s_bus, uint32_t host_gateway_id,
                                           const AudioDataFormat& i2s_format) {
  CopierCfg i2s_in_copier = CreateGatewayCopierCfg(i2s_format, kDspI2sFormat, i2s_gateway_id);
  CopierCfg host_in_copier = CreateGatewayCopierCfg(kDspI2sFormat, kHostI2sFormat, host_gateway_id);
  zx::result<std::vector<uint8_t>> i2s_in_gateway_cfg =
      GetModuleConfig(nhlt, i2s_bus, NHLT_DIRECTION_CAPTURE, NHLT_LINK_TYPE_SSP, i2s_in_copier);
  if (!i2s_in_gateway_cfg.is_ok()) {
    return zx::error(i2s_in_gateway_cfg.status_value());
  }

  return CreateSimplePipeline(controller, {
                                              // Copy from I2S.
                                              {copier_module_id, i2s_in_gateway_cfg.value()},
                                              // Copy to host DMA.
                                              {copier_module_id, RawBytesOf(&host_in_copier)},
                                          });
}

// Get the module ID corresponding of the given module name.
zx::result<uint16_t> GetModuleId(DspModuleController* controller, const char* name) {
  // Read available modules.
  zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> module_list =
      controller->ReadModuleDetails();
  if (!module_list.is_ok()) {
    return zx::error(module_list.status_value());
  }
  auto& modules = module_list.value();

  // Fetch out the copier module.
  auto copier_it = modules.find(name);
  if (copier_it == modules.end()) {
    GLOBAL_LOG(ERROR, "DSP doesn't have support for module '%s'", name);
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(std::move(copier_it->second->module_id));
}

// Eve module config parameters extracted from kbl_i2s_chrome.conf

// Format used by the Eve's ALC5663 headphone codec.
constexpr AudioDataFormat kEveFormatAlc5663 = {
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

zx::result<std::vector<DspStream>> SetUpPixelbookEvePipelines(const Nhlt& nhlt,
                                                              DspModuleController* controller) {
  // Get the ID of the "COPIER" module.
  zx::result<uint16_t> copier_module = GetModuleId(controller, "COPIER");
  if (!copier_module.is_ok()) {
    return zx::error(copier_module.status_value());
  }
  uint16_t copier_module_id = copier_module.value();

  // Create output pipeline to MAX98927 codec.
  constexpr AudioDataFormat kFormatMax98927 = kFormatI2S0Bus;
  zx::result<DspPipelineId> speakers_id = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 0),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S0_BUS, 0), I2S0_BUS, kFormatMax98927);
  if (!speakers_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route to MAX98927 codec");
    return zx::error(speakers_id.status_value());
  }

  // Create output pipeline to ALC5663 codec.
  zx::result<DspPipelineId> headphones_id = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 1),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S1_BUS, 0), I2S1_BUS, kEveFormatAlc5663);
  if (!headphones_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route to ALC5663 codec");
    return zx::error(headphones_id.status_value());
  }

  // Create input pipeline from DMIC.
  constexpr AudioDataFormat kFormatDmics = kFormatI2S0Bus;
  zx::result<DspPipelineId> microphones_id =
      ConnectI2SToHost(nhlt, controller, copier_module_id,
                       I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_INPUT, I2S0_BUS, 0), I2S0_BUS,
                       HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 0), kFormatDmics);
  if (!microphones_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route from DMIC");
    return zx::error(microphones_id.status_value());
  }

  std::vector<DspStream> pipelines;
  pipelines.push_back({.id = speakers_id.value(),
                       .host_format = kHostI2sFormat,
                       .stream_id = 1,
                       .is_input = false,
                       .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS,
                       .name = "Builtin Speakers"});
  pipelines.push_back({.id = microphones_id.value(),
                       .host_format = kFormatDmics,
                       .stream_id = 2,
                       .is_input = true,
                       .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE,
                       .name = "Builtin Microphones"});
  pipelines.push_back({.id = headphones_id.value(),
                       .host_format = kHostI2sFormat,
                       .stream_id = 3,
                       .is_input = false,
                       .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
                       .name = "Builtin Headphone Jack"});
  return zx::ok(std::move(pipelines));
}

constexpr AudioDataFormat kAtlasFormatDmics = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_16BIT,
    .channel_map = 0xFFFF3210,
    .channel_config = ChannelConfig::CONFIG_QUATRO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 4,
    .valid_bit_depth = 16,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};
const AudioDataFormat kAtlasDspFormatInput = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_16BIT,
    .channel_map = 0xFFFF3210,
    .channel_config = ChannelConfig::CONFIG_QUATRO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 4,
    .valid_bit_depth = 16,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};
constexpr AudioDataFormat kAtlasHostFormatInput = {
    .sampling_frequency = SamplingFrequency::FS_48000HZ,
    .bit_depth = BitDepth::DEPTH_16BIT,
    .channel_map = 0xFFFF3210,
    .channel_config = ChannelConfig::CONFIG_QUATRO,
    .interleaving_style = InterleavingStyle::PER_CHANNEL,
    .number_of_channels = 4,
    .valid_bit_depth = 16,
    .sample_type = SampleType::INT_MSB,
    .reserved = 0,
};

zx::result<DspPipelineId> ConnectAtlasDmicToHost(const Nhlt& nhlt, DspModuleController* controller,
                                                 uint16_t copier_module_id,
                                                 uint32_t host_gateway_id, uint32_t dmic_gateway_id,
                                                 uint8_t dmic_bus) {
  CopierCfg dmic_in_copier =
      CreateGatewayCopierCfg(kAtlasFormatDmics, kAtlasDspFormatInput, dmic_gateway_id);
  CopierCfg host_in_copier =
      CreateGatewayCopierCfg(kAtlasDspFormatInput, kAtlasHostFormatInput, host_gateway_id);
  zx::result<std::vector<uint8_t>> dmic_in_gateway_cfg =
      GetModuleConfig(nhlt, dmic_bus, NHLT_DIRECTION_CAPTURE, NHLT_LINK_TYPE_PDM, dmic_in_copier);
  if (!dmic_in_gateway_cfg.is_ok()) {
    return zx::error(dmic_in_gateway_cfg.status_value());
  }

  return CreateSimplePipeline(controller, {
                                              // Copy from DMIC.
                                              {copier_module_id, dmic_in_gateway_cfg.value()},
                                              // Copy to host DMA.
                                              {copier_module_id, RawBytesOf(&host_in_copier)},
                                          });
}

zx::result<std::vector<DspStream>> SetUpPixelbookAtlasPipelines(const Nhlt& nhlt,
                                                                DspModuleController* controller) {
  // Get the ID of the "COPIER" module.
  zx::result<uint16_t> copier_module = GetModuleId(controller, "COPIER");
  if (!copier_module.is_ok()) {
    return zx::error(copier_module.status_value());
  }
  uint16_t copier_module_id = copier_module.value();

  // Create output pipeline to Maxim98373 codec.
  constexpr AudioDataFormat kFormatMax98373 = kFormatI2S0Bus;
  zx::result<DspPipelineId> speakers_id = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 0),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S0_BUS, 0), I2S0_BUS, kFormatMax98373);
  if (!speakers_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route to Max98373 codec");
    return zx::error(speakers_id.status_value());
  }

  constexpr AudioDataFormat kFormatDa7219 = kFormatI2S1Bus;

  // Create output pipeline to DA7219 codec.
  zx::result<DspPipelineId> headset_output_id = ConnectHostToI2S(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 1),
      I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, I2S1_BUS, 0), I2S1_BUS, kFormatDa7219);
  if (!headset_output_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route to output from DA7219 codec");
    return zx::error(headset_output_id.status_value());
  }

  // Create input pipeline to DA7219 codec.
  zx::result<DspPipelineId> headset_input_id =
      ConnectI2SToHost(nhlt, controller, copier_module_id,
                       I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_INPUT, I2S1_BUS, 0), I2S1_BUS,
                       HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 1), kFormatDa7219);
  if (!headset_input_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route to input into DA7219 codec");
    return zx::error(headset_input_id.status_value());
  }

  // Create input pipeline from DMICs.
  // PDM bus must be zero, only one PDM link from SW/FW point of view.
  constexpr uint8_t kDmicBus = 0;
  zx::result<DspPipelineId> microphones_id = ConnectAtlasDmicToHost(
      nhlt, controller, copier_module_id, HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 0),
      DMIC_GATEWAY_CFG_NODE_ID(DMA_TYPE_DMIC_LINK_INPUT, kDmicBus, 0), kDmicBus);
  if (!microphones_id.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not set up route from DMICs");
    return zx::error(microphones_id.status_value());
  }

  AudioDataFormat i2s_actual_format = kFormatI2S0Bus;
  // The slot size in Atlas' output pipeline to the Maxims is actually 16 bits.
  i2s_actual_format.bit_depth = BitDepth::DEPTH_16BIT;

  std::vector<DspStream> streams;
  streams.push_back({.id = speakers_id.value(),
                     .host_format = kHostI2sFormat,
                     .dai_format = i2s_actual_format,
                     .is_i2s = false,
                     .stream_id = 1,
                     .is_input = false,
                     .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS,
                     .name = "Builtin Speakers"});
  streams.push_back({.id = microphones_id.value(),
                     .host_format = kAtlasHostFormatInput,
                     .dai_format = kFormatI2S0Bus,
                     .is_i2s = false,
                     .stream_id = 2,
                     .is_input = true,
                     .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE,
                     .name = "Builtin Microphones"});
  streams.push_back({.id = headset_output_id.value(),
                     .host_format = kHostI2sFormat,
                     .dai_format = kFormatI2S1Bus,
                     .is_i2s = true,
                     .stream_id = 3,
                     .is_input = false,
                     .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
                     .name = "Builtin Headphone Jack Output"});
  streams.push_back({.id = headset_input_id.value(),
                     .host_format = kHostI2sFormat,
                     .dai_format = kFormatI2S1Bus,
                     .is_i2s = true,
                     .stream_id = 4,
                     .is_input = true,
                     .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
                     .name = "Builtin Headphone Jack Input"});
  return zx::ok(std::move(streams));
}

zx::result<> IntelDsp::StartPipeline(DspPipelineId id) {
  // Pipeline must be paused before starting.
  if (zx::result status = module_controller_->SetPipelineState(id, PipelineState::PAUSED, true);
      !status.is_ok()) {
    return zx::error(status.status_value());
  }

  // Start the pipeline.
  if (zx::result status = module_controller_->SetPipelineState(id, PipelineState::RUNNING, true);
      !status.is_ok()) {
    return zx::error(status.status_value());
  }

  return zx::ok();
}

zx::result<> IntelDsp::PausePipeline(DspPipelineId id) {
  if (zx::result status = module_controller_->SetPipelineState(id, PipelineState::PAUSED, true);
      !status.is_ok()) {
    return zx::error(status.status_value());
  }

  if (zx::result status = module_controller_->SetPipelineState(id, PipelineState::RESET, true);
      !status.is_ok()) {
    return zx::error(status.status_value());
  }

  return zx::ok();
}

zx::result<> IntelDsp::CreateAndStartStreams() {
  zx_status_t res = ZX_OK;

  // Setup the pipelines.
  zx::result<std::vector<DspStream>> streams;
  // TODO(fxbug.dev/84323): Remove this hardcoded topology decisions for Atlas or Eve and add a
  // topology loading infrastructure that would render this unnecessary.
  if (nhlt_->IsOemMatch("GOOGLE", "ATLASMAX")) {
    streams = SetUpPixelbookAtlasPipelines(*nhlt_, module_controller_.get());
    if (!streams.is_ok()) {
      GLOBAL_LOG(ERROR, "Failed to set up DSP pipelines: %s", streams.status_string());
      return zx::error(streams.status_value());
    }
  } else if (nhlt_->IsOemMatch("GOOGLE", "EVEMAX")) {
    streams = SetUpPixelbookEvePipelines(*nhlt_, module_controller_.get());
    if (!streams.is_ok()) {
      GLOBAL_LOG(ERROR, "Failed to set up DSP pipelines: %s", streams.status_string());
      return zx::error(streams.status_value());
    }
  } else {
    LOG(ERROR, "Board not supported to set up DSP pipelines");
  }
  for (const auto& stream_def : streams.value()) {
    auto stream = fbl::AdoptRef(new IntelDspStream(stream_def));

    res = ActivateStream(stream);
    if (res != ZX_OK) {
      LOG(ERROR, "Failed to activate %s stream id #%u (res %d)!",
          stream_def.is_input ? "input" : "output", stream_def.stream_id, res);
      return zx::error(res);
    }
  }

  return zx::ok();
}

}  // namespace intel_hda
}  // namespace audio
