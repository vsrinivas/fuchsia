// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-audio-dsp.h"

namespace audio {
namespace intel_hda {

namespace {
constexpr const char* I2S_CFG_PATH       = "/boot/lib/firmware/max98927-render-2ch-48khz-16b.bin";

// Module config parameters extracted from kbl_i2s_chrome.conf

// Set up 2 pipelines for system playback:
// 1. copier[host DMA]->mixin
// 2. mixout->copier[I2S DMA]
// 2 Pipelines are needed because only one instance of a module can exist in a pipeline.
constexpr uint8_t PIPELINE0_ID = 0;
constexpr uint8_t PIPELINE1_ID = 1;

// Set up 2 pipelines for system capture:
// 2. copier[I2S DMA]->mixin
// 3. mixout->copier[host DMA]
// 2 Pipelines are needed because only one instance of a module can exist in a pipeline.
constexpr uint8_t PIPELINE2_ID = 2;
constexpr uint8_t PIPELINE3_ID = 3;

// Module instance IDs.
constexpr uint8_t HOST_OUT_COPIER_ID = 0;
constexpr uint8_t I2S0_OUT_COPIER_ID = 1;
constexpr uint8_t I2S0_IN_COPIER_ID  = 2;
constexpr uint8_t HOST_IN_COPIER_ID  = 3;

constexpr uint8_t HOST_OUT_MIXIN_ID  = 0;
constexpr uint8_t I2S0_IN_MIXIN_ID   = 1;

constexpr uint8_t I2S0_OUT_MIXOUT_ID = 0;
constexpr uint8_t HOST_IN_MIXOUT_ID  = 1;

const struct PipelineConfig {
    uint8_t id;
    uint8_t priority;
    uint8_t mem_pages;
    bool lp;
} PIPELINE_CFG[] = {
{
    .id = PIPELINE0_ID,
    .priority = 0,
    .mem_pages = 2,
    .lp = true, // false in config, keep running in low power mode for dev
},
{
    .id = PIPELINE1_ID,
    .priority = 0,
    .mem_pages = 4,
    .lp = true,
},
{
    .id = PIPELINE2_ID,
    .priority = 0,
    .mem_pages = 2,
    .lp = true,
},
{
    .id = PIPELINE3_ID,
    .priority = 0,
    .mem_pages = 2,
    .lp = true,
},
};

// Use 48khz 16-bit stereo throughout
const AudioDataFormat FMT_HOST = {
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

const AudioDataFormat FMT_I2S = {
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
    .base_cfg = {
        .cpc = 100000,
        .ibs = 384,
        .obs = 384,
        .is_pages = 0,
        .audio_fmt = {
            .sampling_frequency = FMT_HOST.sampling_frequency,
            .bit_depth = FMT_HOST.bit_depth,
            .channel_map = FMT_HOST.channel_map,
            .channel_config = FMT_HOST.channel_config,
            .interleaving_style = FMT_HOST.interleaving_style,
            .number_of_channels = FMT_HOST.number_of_channels,
            .valid_bit_depth = FMT_HOST.valid_bit_depth,
            .sample_type = FMT_HOST.sample_type,
            .reserved = 0,
        },
    },
    .out_fmt = {
        .sampling_frequency = FMT_MIXER.sampling_frequency,
        .bit_depth = FMT_MIXER.bit_depth,
        .channel_map = FMT_MIXER.channel_map,
        .channel_config = FMT_MIXER.channel_config,
        .interleaving_style = FMT_MIXER.interleaving_style,
        .number_of_channels = FMT_MIXER.number_of_channels,
        .valid_bit_depth = FMT_MIXER.valid_bit_depth,
        .sample_type = FMT_MIXER.sample_type,
        .reserved = 0,
    },
    .copier_feature_mask = 0,
    .gtw_cfg = {
        .node_id = HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_OUTPUT, 0),
        .dma_buffer_size = 2 * 384,
        .config_length = 0,
    },
};

const CopierCfg HOST_IN_COPIER_CFG = {
    .base_cfg = {
        .cpc = 100000,
        .ibs = 384,
        .obs = 384,
        .is_pages = 0,
        .audio_fmt = {
            .sampling_frequency = FMT_MIXER.sampling_frequency,
            .bit_depth = FMT_MIXER.bit_depth,
            .channel_map = FMT_MIXER.channel_map,
            .channel_config = FMT_MIXER.channel_config,
            .interleaving_style = FMT_MIXER.interleaving_style,
            .number_of_channels = FMT_MIXER.number_of_channels,
            .valid_bit_depth = FMT_MIXER.valid_bit_depth,
            .sample_type = FMT_MIXER.sample_type,
            .reserved = 0,
        },
    },
    .out_fmt = {
        .sampling_frequency = FMT_HOST.sampling_frequency,
        .bit_depth = FMT_HOST.bit_depth,
        .channel_map = FMT_HOST.channel_map,
        .channel_config = FMT_HOST.channel_config,
        .interleaving_style = FMT_HOST.interleaving_style,
        .number_of_channels = FMT_HOST.number_of_channels,
        .valid_bit_depth = FMT_HOST.valid_bit_depth,
        .sample_type = FMT_HOST.sample_type,
        .reserved = 0,
    },
    .copier_feature_mask = 0,
    .gtw_cfg = {
        .node_id = HDA_GATEWAY_CFG_NODE_ID(DMA_TYPE_HDA_HOST_INPUT, 0),
        .dma_buffer_size = 2 * 384,
        .config_length = 0,
    },
};

const CopierCfg I2S_OUT_COPIER_CFG = {
    .base_cfg = {
        .cpc = 100000,
        .ibs = 384,
        .obs = 384,
        .is_pages = 0,
        .audio_fmt = {
            .sampling_frequency = FMT_MIXER.sampling_frequency,
            .bit_depth = FMT_MIXER.bit_depth,
            .channel_map = FMT_MIXER.channel_map,
            .channel_config = FMT_MIXER.channel_config,
            .interleaving_style = FMT_MIXER.interleaving_style,
            .number_of_channels = FMT_MIXER.number_of_channels,
            .valid_bit_depth = FMT_MIXER.valid_bit_depth,
            .sample_type = FMT_MIXER.sample_type,
            .reserved = 0,
        },
    },
    .out_fmt = {
        .sampling_frequency = FMT_I2S.sampling_frequency,
        .bit_depth = FMT_I2S.bit_depth,
        .channel_map = FMT_I2S.channel_map,
        .channel_config = FMT_I2S.channel_config,
        .interleaving_style = FMT_I2S.interleaving_style,
        .number_of_channels = FMT_I2S.number_of_channels,
        .valid_bit_depth = FMT_I2S.valid_bit_depth,
        .sample_type = FMT_I2S.sample_type,
        .reserved = 0,
    },
    .copier_feature_mask = 0,
    .gtw_cfg = {
        .node_id = I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_OUTPUT, 0, 0),
        .dma_buffer_size = 2 * 384,
        .config_length = 0,
    },
};

const CopierCfg I2S_IN_COPIER_CFG = {
    .base_cfg = {
        .cpc = 100000,
        .ibs = 384,
        .obs = 384,
        .is_pages = 0,
        .audio_fmt = {
            .sampling_frequency = FMT_I2S.sampling_frequency,
            .bit_depth = FMT_I2S.bit_depth,
            .channel_map = FMT_I2S.channel_map,
            .channel_config = FMT_I2S.channel_config,
            .interleaving_style = FMT_I2S.interleaving_style,
            .number_of_channels = FMT_I2S.number_of_channels,
            .valid_bit_depth = FMT_I2S.valid_bit_depth,
            .sample_type = FMT_I2S.sample_type,
            .reserved = 0,
        },
    },
    .out_fmt = {
        .sampling_frequency = FMT_MIXER.sampling_frequency,
        .bit_depth = FMT_MIXER.bit_depth,
        .channel_map = FMT_MIXER.channel_map,
        .channel_config = FMT_MIXER.channel_config,
        .interleaving_style = FMT_MIXER.interleaving_style,
        .number_of_channels = FMT_MIXER.number_of_channels,
        .valid_bit_depth = FMT_MIXER.valid_bit_depth,
        .sample_type = FMT_MIXER.sample_type,
        .reserved = 0,
    },
    .copier_feature_mask = 0,
    .gtw_cfg = {
        .node_id = I2S_GATEWAY_CFG_NODE_ID(DMA_TYPE_I2S_LINK_INPUT, 0, 0),
        .dma_buffer_size = 2 * 384,
        .config_length = 0,
    },
};

const BaseModuleCfg MIXER_CFG = {
    .cpc = 100000,
    .ibs = 384,
    .obs = 384,
    .is_pages = 0,
    .audio_fmt = {
        .sampling_frequency = FMT_MIXER.sampling_frequency,
        .bit_depth = FMT_MIXER.bit_depth,
        .channel_map = FMT_MIXER.channel_map,
        .channel_config = FMT_MIXER.channel_config,
        .interleaving_style = FMT_MIXER.interleaving_style,
        .number_of_channels = FMT_MIXER.number_of_channels,
        .valid_bit_depth = FMT_MIXER.valid_bit_depth,
        .sample_type = FMT_MIXER.sample_type,
        .reserved = 0,
    },
};

}  // anon namespace

zx_status_t IntelAudioDsp::CreateHostDmaModule(uint8_t instance_id, uint8_t pipeline_id,
                                               const CopierCfg& cfg) {
    return ipc_.InitInstance(module_ids_[Module::COPIER],
                             instance_id,
                             ProcDomain::LOW_LATENCY,
                             0,
                             pipeline_id,
                             sizeof(cfg),
                             &cfg);
}

zx_status_t IntelAudioDsp::CreateI2SModule(uint8_t instance_id, uint8_t pipeline_id,
                                           const CopierCfg& cfg, const zx::vmo& i2s_cfg,
                                           size_t i2s_cfg_size) {
    size_t cfg_size = sizeof(cfg) + i2s_cfg_size;
    ZX_DEBUG_ASSERT(cfg_size <= UINT16_MAX);

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> cfg_buf(new (&ac) uint8_t[cfg_size]);
    if (!ac.check()) {
        LOG(ERROR, "out of memory while attempting to allocate copier config buffer\n");
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t st = i2s_cfg.read(cfg_buf.get() + sizeof(cfg), 0, i2s_cfg_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error reading I2S config blob VMO (err %d)\n", st);
        return st;
    }

    memcpy(cfg_buf.get(), &cfg, sizeof(cfg));
    auto copier_cfg = reinterpret_cast<CopierCfg*>(cfg_buf.get());
    copier_cfg->gtw_cfg.config_length = static_cast<uint32_t>(i2s_cfg_size);

    return ipc_.InitInstance(module_ids_[Module::COPIER],
                             instance_id,
                             ProcDomain::LOW_LATENCY,
                             0,
                             pipeline_id,
                             static_cast<uint16_t>(cfg_size),
                             cfg_buf.get());
}

zx_status_t IntelAudioDsp::CreateMixinModule(uint8_t instance_id, uint8_t pipeline_id,
                                             const BaseModuleCfg& cfg) {
    return ipc_.InitInstance(module_ids_[Module::MIXIN],
                             instance_id,
                             ProcDomain::LOW_LATENCY,
                             0,
                             pipeline_id,
                             sizeof(cfg),
                             &cfg);
}

zx_status_t IntelAudioDsp::CreateMixoutModule(uint8_t instance_id, uint8_t pipeline_id,
                                              const BaseModuleCfg& cfg) {
    return ipc_.InitInstance(module_ids_[Module::MIXOUT],
                             instance_id,
                             ProcDomain::LOW_LATENCY,
                             0,
                             pipeline_id,
                             sizeof(cfg),
                             &cfg);
}

zx_status_t IntelAudioDsp::SetupPipelines() {
    ZX_DEBUG_ASSERT(module_ids_[Module::COPIER] != 0);
    ZX_DEBUG_ASSERT(module_ids_[Module::MIXIN] != 0);
    ZX_DEBUG_ASSERT(module_ids_[Module::MIXOUT] != 0);

    zx_status_t st = ZX_OK;

    // Create pipelines
    for (const auto& cfg : PIPELINE_CFG) {
        st = ipc_.CreatePipeline(cfg.id, cfg.priority, cfg.mem_pages, cfg.lp);
        if (st != ZX_OK) {
            return st;
        }
    }

    // Create pipeline 0 modules. Host DMA -> mixin
    // Modules must be created in order of source -> sink
    st = CreateHostDmaModule(HOST_OUT_COPIER_ID, PIPELINE0_ID, HOST_OUT_COPIER_CFG);
    if (st != ZX_OK) {
        return st;
    }
    st = CreateMixinModule(HOST_OUT_MIXIN_ID, PIPELINE0_ID, MIXER_CFG);
    if (st != ZX_OK) {
        return st;
    }

    // Bind pipeline 0
    st = ipc_.Bind(module_ids_[Module::COPIER], HOST_OUT_COPIER_ID, 0,
                   module_ids_[Module::MIXIN], HOST_OUT_MIXIN_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    // Create pipeline 1 modules. mixout -> I2S DMA
    st = CreateMixoutModule(I2S0_OUT_MIXOUT_ID, PIPELINE1_ID, MIXER_CFG);
    if (st != ZX_OK) {
        return st;
    }

    // Get the VMO containing the I2S config blob
    // TODO(yky): this should come from ACPI (NHLT table)
    zx::vmo blob_vmo;
    size_t blob_size;
    st = load_firmware(codec_device(), I2S_CFG_PATH, blob_vmo.reset_and_get_address(), &blob_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error getting I2S config blob (err %d)\n", st);
        return st;
    }

    st = CreateI2SModule(I2S0_OUT_COPIER_ID, PIPELINE1_ID, I2S_OUT_COPIER_CFG, blob_vmo, blob_size);
    if (st != ZX_OK) {
        return st;
    }

    // Bind pipeline 1
    st = ipc_.Bind(module_ids_[Module::MIXOUT], I2S0_OUT_MIXOUT_ID, 0,
                   module_ids_[Module::COPIER], I2S0_OUT_COPIER_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    // Create pipeline 2 modules. I2S DMA -> mixin
    st = CreateI2SModule(I2S0_IN_COPIER_ID, PIPELINE2_ID, I2S_IN_COPIER_CFG, blob_vmo, blob_size);
    if (st != ZX_OK) {
        return st;
    }
    st = CreateMixinModule(I2S0_IN_MIXIN_ID, PIPELINE2_ID, MIXER_CFG);
    if (st != ZX_OK) {
        return st;
    }

    // Bind pipeline 2
    st = ipc_.Bind(module_ids_[Module::COPIER], I2S0_IN_COPIER_ID, 0,
                   module_ids_[Module::MIXIN], I2S0_IN_MIXIN_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    // Create pipeline 3 modules. mixout -> Host DMA
    st = CreateMixoutModule(HOST_IN_MIXOUT_ID, PIPELINE3_ID, MIXER_CFG);
    if (st != ZX_OK) {
        return st;
    }
    st = CreateHostDmaModule(HOST_IN_COPIER_ID, PIPELINE3_ID, HOST_IN_COPIER_CFG);
    if (st != ZX_OK) {
        return st;
    }

    // Bind pipeline 2
    st = ipc_.Bind(module_ids_[Module::MIXOUT], HOST_IN_MIXOUT_ID, 0,
                   module_ids_[Module::COPIER], HOST_IN_COPIER_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    // Bind playback pipeline
    st = ipc_.Bind(module_ids_[Module::MIXIN], HOST_OUT_MIXIN_ID, 0,
                   module_ids_[Module::MIXOUT], I2S0_OUT_MIXOUT_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    // Bind capture pipeline
    st = ipc_.Bind(module_ids_[Module::MIXIN], I2S0_IN_MIXIN_ID, 0,
                   module_ids_[Module::MIXOUT], HOST_IN_MIXOUT_ID, 0);
    if (st != ZX_OK) {
        return st;
    }

    return ZX_OK;
}

zx_status_t IntelAudioDsp::StartPipelines() {
    zx_status_t st = RunPipeline(PIPELINE1_ID);
    if (st != ZX_OK) {
        return st;
    }
    return RunPipeline(PIPELINE0_ID);
    // TODO Error recovery
}

zx_status_t IntelAudioDsp::PausePipelines() {
    zx_status_t st = ipc_.SetPipelineState(PIPELINE0_ID, PipelineState::PAUSED, true);
    if (st != ZX_OK) {
        return st;
    }
    st = ipc_.SetPipelineState(PIPELINE1_ID, PipelineState::PAUSED, true);
    if (st != ZX_OK) {
        return st;
    }
    // Reset DSP DMA
    st = ipc_.SetPipelineState(PIPELINE0_ID, PipelineState::RESET, true);
    if (st != ZX_OK) {
        return st;
    }
    return ipc_.SetPipelineState(PIPELINE1_ID, PipelineState::RESET, true);
    // TODO Error recovery
}

}  // namespace intel_hda
}  // namespace audio
