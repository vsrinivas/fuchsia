// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_UTILS_INTEL_AUDIO_DSP_IPC_H_
#define INTEL_HDA_UTILS_INTEL_AUDIO_DSP_IPC_H_

#include <zircon/compiler.h>

/**
 * Host <-> DSP IPC interface definitions taken from
 *
 * cAVS Audio DSP
 * Audio DSP Firmware Interface Specification
 * Revision 0.5
 * September 2017
 *
 * And
 *
 * cAVS Audio DSP Modules Specification
 * Audio DSP Firmware Modules Interface Specification
 * Revision 0.5
 * September 2017
 */

#ifdef __cplusplus

namespace audio {
namespace intel_hda {

// Common structures

enum class MsgTarget : uint8_t {
  FW_GEN_MSG = 0u,
  MODULE_MSG = 1u,
};

enum class MsgDir : uint8_t {
  MSG_REQUEST = 0u,
  MSG_NOTIFICATION = 0u,
  MSG_REPLY = 1u,
};

// Global Message

enum class GlobalType : uint8_t {
  CREATE_PIPELINE = 17u,
  SET_PIPELINE_STATE = 19u,
  NOTIFICATION = 27u,
};

// Module Message

enum class ModuleMsgType : uint8_t {
  INIT_INSTANCE = 0u,
  MODULE_CONFIG_GET = 1u,
  MODULE_CONFIG_SET = 2u,
  LARGE_CONFIG_GET = 3u,
  LARGE_CONFIG_SET = 4u,
  BIND = 5u,
  UNBIND = 6u,
  SET_DX = 7u,
  ENTER_MODULE_RESTORE = 9u,
  EXIT_MODULE_RESTORE = 10u,
  DELETE_INSTANCE = 11u,
};

// Message Reply

enum class MsgStatus : uint32_t {
  IPC_SUCCESS = 0u,
  MOD_NOT_INITIALIZED = 104u,
};

// Notifications

enum class NotificationType : uint8_t {
  PHRASE_DETECTED = 4u,
  RESOURCE_EVENT = 5u,
  LOG_BUFFER_STATUS = 6u,
  TIMESTAMP_CAPTURED = 7u,
  FW_READY = 8u,
  EXCEPTION_CAUGHT = 10u,
};

enum class ResourceType : uint32_t {
  MODULE_INSTANCE = 0u,
  PIPELINE = 1u,
  GATEWAY = 2u,
  EDF_TASK = 3u,
};

enum class ResourceEventType : uint32_t {
  BUDGET_VIOLATION = 0u,
  MIXER_UNDERRUN_DETECTED = 1u,
  STREAM_DATA_SEGMENT = 2u,
  PROCESS_DATA_ERROR = 3u,
  STACK_OVERFLOW = 4u,
  BUFFERING_MODE_CHANGED = 5u,
  GATEWAY_UNDERRUN_DETECTED = 6u,
  EDF_DOMAIN_UNSTABLE = 7u,
  WATCHDOG_EXPIRED = 8u,
  GATEWAY_HIGH_THRES = 10u,
  GATEWAY_LOW_THRES = 11u,
};

struct ResourceEventData {
  uint32_t resource_type;
  uint32_t resource_id;
  uint32_t event_type;
  uint32_t event_data[6];
};

// Init Instance

enum class ProcDomain : uint8_t {
  LOW_LATENCY = 0u,
  DATA_PROCESSING = 1u,
};

enum class SamplingFrequency : uint32_t {
  FS_8000HZ = 8000u,
  FS_11025HZ = 11025u,
  FS_12000HZ = 12000u,
  FS_16000HZ = 16000u,
  FS_22050HZ = 22050u,
  FS_24000HZ = 24000u,
  FS_32000HZ = 32000u,
  FS_44100HZ = 44100u,
  FS_48000HZ = 48000u,
  FS_64000HZ = 64000u,
  FS_88200HZ = 88200u,
  FS_96000HZ = 96000u,
  FS_128000HZ = 128000u,
  FS_176000HZ = 176000u,
  FS_192000HZ = 192000u,
};

enum class BitDepth : uint32_t {
  DEPTH_8BIT = 8u,
  DEPTH_16BIT = 16u,
  DEPTH_24BIT = 24u,
  DEPTH_32BIT = 32u,
};

enum ChannelIndex : uint8_t {
  LEFT = 0u,
  CENTER = 1u,
  RIGHT = 2u,
  LEFT_SURROUND = 3u,
  CENTER_SURROUND = 3u,
  RIGHT_SURROUND = 4u,
  LEFT_SIDE = 5u,
  RIGHT_SIDE = 6u,
  LFE = 7u,
  INVALID = 0xF,
};

enum ChannelConfig : uint32_t {
  CONFIG_MONO = 0u,
  CONFIG_STEREO = 1u,
  CONFIG_2_POINT_1 = 2u,
  CONFIG_3_POINT_0 = 3u,
  CONFIG_3_POINT_1 = 4u,
  CONFIG_QUATRO = 5u,
  CONFIG_4_POINT_0 = 6u,
  CONFIG_5_POINT_0 = 7u,
  CONFIG_5_POINT_1 = 8u,
  CONFIG_DUAL_MONO = 9u,
  CONFIG_I2S_DUAL_STEREO_0 = 10u,
  CONFIG_I2S_DUAL_STEREO_1 = 11u,
};

enum InterleavingStyle : uint32_t {
  PER_CHANNEL = 0,
  PER_SAMPLE = 1,
};

enum SampleType : uint8_t {
  INT_MSB = 0u,
  INT_LSB = 1u,
  INT_SIGNED = 2u,
  INT_UNSIGNED = 3u,
  FLOAT = 4u,
};

struct AudioDataFormat {
  SamplingFrequency sampling_frequency;
  BitDepth bit_depth;
  uint32_t channel_map;
  ChannelConfig channel_config;
  InterleavingStyle interleaving_style;
  uint8_t number_of_channels;
  uint8_t valid_bit_depth;
  SampleType sample_type;
  uint8_t reserved;
} __PACKED;
static_assert(sizeof(AudioDataFormat) == 24, "invalid AudioDataFormat size\n");

struct BaseModuleCfg {
  uint32_t cpc;               // DSP cycles required to process one input frame.
  uint32_t ibs;               // Size of module's input frame, in bytes.
  uint32_t obs;               // Size of module's output frame, in bytes.
  uint32_t is_pages;          // Number of memory pages to be allocated for this module.
  AudioDataFormat audio_fmt;  // Format of the module's input data.
} __PACKED;

struct IoPinFormat {
  uint32_t pin_index;  // Input/output pin number.
  uint32_t ibs_obs;    // Input/output frame size (in bytes).
  AudioDataFormat audio_fmt;
} __PACKED;

struct BaseModuleCfgExt {
  uint16_t nb_input_pins;   // Number of input pins in |input_output_pins|.
  uint16_t nb_output_pins;  // Number of output pins in |input_output_pins|.
  uint8_t reserved[8];
  uint32_t priv_param_length;  // Length of module-specific parameters for this module.

  // Array of input pins, followed by array of output pins.
  //
  // Input and output pins will not necessarily be contiguous: the
  // |pin_index| is used, not the index of the entry in this array.
  IoPinFormat input_output_pins[];
} __PACKED;

// Pipeline Management

enum class PipelineState : uint16_t {
  INVALID = 0u,
  UNINITIALIZED = 1u,
  RESET = 2u,
  PAUSED = 3u,
  RUNNING = 4u,
  ERROR_STOP = 5u,
  SAVED = 6u,
  RESTORED = 7u,
};

// Common message defines

constexpr uint32_t IPC_PRI_MSG_TGT_MASK = 0x1;
constexpr uint32_t IPC_PRI_MSG_TGT_SHIFT = 30;
constexpr uint32_t IPC_PRI_RSP_MASK = 0x1;
constexpr uint32_t IPC_PRI_RSP_SHIFT = 29;
constexpr uint32_t IPC_PRI_TYPE_MASK = 0x1F;
constexpr uint32_t IPC_PRI_TYPE_SHIFT = 24;
constexpr uint32_t IPC_PRI_INSTANCE_ID_MASK = 0xFF;
constexpr uint32_t IPC_PRI_INSTANCE_ID_SHIFT = 16;
constexpr uint32_t IPC_PRI_MODULE_ID_MASK = 0xFFFF;
constexpr uint32_t IPC_PRI_MODULE_ID_SHIFT = 0;

// Message Reply

constexpr uint32_t IPC_PRI_STATUS_MASK = 0x00FFFFFF;

// Notification

constexpr uint32_t IPC_PRI_NOTIF_TYPE_MASK = 0xFF;
constexpr uint32_t IPC_PRI_NOTIF_TYPE_SHIFT = 16;

// Init Instance Request Parameters

constexpr uint32_t IPC_EXT_PROC_DOMAIN_SHIFT = 28;
constexpr uint32_t IPC_EXT_CORE_ID_MASK = 0xF;
constexpr uint32_t IPC_EXT_CORE_ID_SHIFT = 24;
constexpr uint32_t IPC_EXT_PPL_INSTANCE_ID_SHIFT = 16;

// Large Config Get Request Parameters

constexpr uint32_t IPC_EXT_INIT_BLOCK_SHIFT = 29;
constexpr uint32_t IPC_EXT_FINAL_BLOCK_SHIFT = 28;
constexpr uint32_t IPC_EXT_LARGE_PARAM_ID_MASK = 0xFF;
constexpr uint32_t IPC_EXT_LARGE_PARAM_ID_SHIFT = 20;
constexpr uint32_t IPC_EXT_DATA_OFF_SIZE_MASK = 0x000FFFFF;

// Bind/Unbind Request Parameters

constexpr uint32_t IPC_EXT_DST_INSTANCE_ID_SHIFT = 16;
constexpr uint32_t IPC_EXT_DST_QUEUE_MASK = 0x7;
constexpr uint32_t IPC_EXT_DST_QUEUE_SHIFT = 24;
constexpr uint32_t IPC_EXT_SRC_QUEUE_MASK = 0x7;
constexpr uint32_t IPC_EXT_SRC_QUEUE_SHIFT = 27;

// Create Pipeline Request Parameters

constexpr uint32_t IPC_PRI_PPL_PRIORITY_MASK = 0x1F;
constexpr uint32_t IPC_PRI_PPL_PRIORITY_SHIFT = 11;
constexpr uint32_t IPC_PRI_PPL_MEM_SIZE_MASK = 0x3FF;

// Set Pipeline State Parameters
constexpr uint32_t IPC_EXT_SYNC_STOP_START_SHIFT = 1;

struct IpcMessage {
 public:
  IpcMessage(uint32_t pri, uint32_t ext) : primary(pri), extension(ext) {}
  IpcMessage() {}

  // Common

  MsgTarget msg_tgt() const {
    return static_cast<MsgTarget>((primary >> IPC_PRI_MSG_TGT_SHIFT) & IPC_PRI_MSG_TGT_MASK);
  }
  MsgDir msg_dir() const {
    return static_cast<MsgDir>((primary >> IPC_PRI_RSP_SHIFT) & IPC_PRI_RSP_MASK);
  }
  uint8_t type() const { return (primary >> IPC_PRI_TYPE_SHIFT) & IPC_PRI_TYPE_MASK; }
  uint8_t instance_id() const {
    return (primary >> IPC_PRI_INSTANCE_ID_SHIFT) & IPC_PRI_INSTANCE_ID_MASK;
  }
  uint16_t module_id() const { return primary & IPC_PRI_MODULE_ID_MASK; }

  // Message Reply

  MsgStatus status() const { return static_cast<MsgStatus>(primary & IPC_PRI_STATUS_MASK); }

  bool is_reply() const { return msg_dir() == MsgDir::MSG_REPLY; }

  // Notification

  bool is_notif() const {
    return (msg_tgt() == MsgTarget::FW_GEN_MSG) && (msg_dir() == MsgDir::MSG_NOTIFICATION) &&
           (static_cast<GlobalType>(type()) == GlobalType::NOTIFICATION);
  }

  NotificationType notif_type() const {
    return static_cast<NotificationType>((primary >> IPC_PRI_NOTIF_TYPE_SHIFT) &
                                         IPC_PRI_NOTIF_TYPE_MASK);
  }

  // Large Config Get/Set

  bool init_block() { return static_cast<bool>((extension >> IPC_EXT_INIT_BLOCK_SHIFT) & 0x1); }
  bool final_block() { return static_cast<bool>((extension >> IPC_EXT_FINAL_BLOCK_SHIFT) & 0x1); }
  uint8_t large_param_id() {
    return (extension >> IPC_EXT_LARGE_PARAM_ID_SHIFT) & IPC_EXT_LARGE_PARAM_ID_MASK;
  }
  uint32_t data_off_size() { return extension & IPC_EXT_DATA_OFF_SIZE_MASK; }

  uint32_t primary = 0;
  uint32_t extension = 0;
};

// Common

#define _SIC_ static inline constexpr

_SIC_ uint32_t IPC_PRI(MsgTarget msg_tgt, MsgDir rsp, ModuleMsgType type, uint8_t instance_id,
                       uint16_t module_id) {
  return (static_cast<uint8_t>(msg_tgt) << IPC_PRI_MSG_TGT_SHIFT) |
         (static_cast<uint8_t>(rsp) << IPC_PRI_RSP_SHIFT) |
         (static_cast<uint8_t>(type) << IPC_PRI_TYPE_SHIFT) |
         (instance_id << IPC_PRI_INSTANCE_ID_SHIFT) | module_id;
}

// Init Instance

_SIC_ uint32_t IPC_INIT_INSTANCE_EXT(ProcDomain proc_domain, uint8_t core_id,
                                     uint8_t ppl_instance_id, uint16_t param_block_size) {
  return (static_cast<uint8_t>(proc_domain) << IPC_EXT_PROC_DOMAIN_SHIFT) |
         ((core_id & IPC_EXT_CORE_ID_MASK) << IPC_EXT_CORE_ID_SHIFT) |
         (ppl_instance_id << IPC_EXT_PPL_INSTANCE_ID_SHIFT) | param_block_size;
}

// Large Config Get/Set

_SIC_ uint32_t IPC_LARGE_CONFIG_EXT(bool init_block, bool final_block, uint8_t large_param_id,
                                    uint32_t data_off_size) {
  return (init_block ? (1 << IPC_EXT_INIT_BLOCK_SHIFT) : 0) |
         (final_block ? (1 << IPC_EXT_FINAL_BLOCK_SHIFT) : 0) |
         (large_param_id << IPC_EXT_LARGE_PARAM_ID_SHIFT) |
         (data_off_size & IPC_EXT_DATA_OFF_SIZE_MASK);
}

// Bind/Unbind

_SIC_ uint32_t IPC_BIND_UNBIND_EXT(uint16_t dst_module_id, uint8_t dst_instance_id,
                                   uint8_t dst_queue, uint8_t src_queue) {
  return ((src_queue & IPC_EXT_SRC_QUEUE_MASK) << IPC_EXT_SRC_QUEUE_SHIFT) |
         ((dst_queue & IPC_EXT_DST_QUEUE_MASK) << IPC_EXT_DST_QUEUE_SHIFT) |
         (dst_instance_id << IPC_EXT_DST_INSTANCE_ID_SHIFT) | dst_module_id;
}

// Create Pipeline

_SIC_ uint32_t IPC_CREATE_PIPELINE_PRI(uint8_t instance_id, uint8_t ppl_priority,
                                       uint16_t ppl_mem_size) {
  return (static_cast<uint8_t>(MsgTarget::FW_GEN_MSG) << IPC_PRI_MSG_TGT_SHIFT) |
         (static_cast<uint8_t>(MsgDir::MSG_REQUEST) << IPC_PRI_RSP_SHIFT) |
         (static_cast<uint8_t>(GlobalType::CREATE_PIPELINE) << IPC_PRI_TYPE_SHIFT) |
         (instance_id << IPC_PRI_INSTANCE_ID_SHIFT) |
         ((ppl_priority & IPC_PRI_PPL_PRIORITY_MASK) << IPC_PRI_PPL_PRIORITY_SHIFT) |
         (ppl_mem_size & IPC_PRI_PPL_MEM_SIZE_MASK);
}

_SIC_ uint32_t IPC_CREATE_PIPELINE_EXT(bool lp) { return (lp ? 1 : 0); }

// Set Pipeline State

_SIC_ uint32_t IPC_SET_PIPELINE_STATE_PRI(uint8_t ppl_id, PipelineState state) {
  return (static_cast<uint8_t>(MsgTarget::FW_GEN_MSG) << IPC_PRI_MSG_TGT_SHIFT) |
         (static_cast<uint8_t>(MsgDir::MSG_REQUEST) << IPC_PRI_RSP_SHIFT) |
         (static_cast<uint8_t>(GlobalType::SET_PIPELINE_STATE) << IPC_PRI_TYPE_SHIFT) |
         (ppl_id << IPC_PRI_INSTANCE_ID_SHIFT) | static_cast<uint16_t>(state);
}

_SIC_ uint32_t IPC_SET_PIPELINE_STATE_EXT(bool multi_ppl, bool sync_stop_start) {
  return (sync_stop_start ? (1 << IPC_EXT_SYNC_STOP_START_SHIFT) : 0) | (multi_ppl ? 1 : 0);
}

#undef _SIC_

// Base FW Run-time Parameters

enum class BaseFWParamType : uint8_t {
  ADSP_PROPERTIES = 0u,
  ADSP_RESOURCE_STATE = 1u,
  NOTIFICATION_MASK = 3u,
  ASTATE_TABLE = 4u,
  DMA_CONTROL = 5u,
  ENABLE_LOGS = 6u,
  FIRMWARE_CONFIG = 7u,
  HARDWARE_CONFIG = 8u,
  MODULES_INFO = 9u,
  PIPELINE_LIST_INFO = 10u,
  PIPELINE_PROPS = 11u,
  SCHEDULERS_INFO = 12u,
  GATEWAYS_INFO = 13u,
  MEMORY_STATE_INFO = 14u,
  POWER_STATE_INFO = 15u,
  LIBRARIES_INFO = 16u,
  PERF_MEASUREMENTS_STATE = 17u,
  GLOBAL_PERF_DATA = 18u,
  L2_CACHE_INFO = 19u,
  SYSTEM_TIME = 20u,
};

enum class FirmwareConfigType : uint32_t {
  FW_VERSION = 0u,
  MEMORY_RECLAIMED = 1u,
  SLOW_CLOCK_FREQ_HZ = 2u,
  FAST_CLOCK_FREQ_HZ = 3u,
  DMA_BUFFER_CONFIG = 4u,
  ALH_SUPPORT_LEVEL = 5u,
  IPC_DL_MAILBOX_BYTES = 6u,
  IPC_UL_MAILBOX_BYTES = 7u,
  TRACE_LOG_BYTES = 8u,
  MAX_PPL_COUNT = 9u,
  MAX_ASTATE_COUNT = 10u,
  MAX_MODULE_PIN_COUNT = 11u,
  MODULES_COUNT = 12u,
  MAX_MOD_INST_COUNT = 13u,
  MAX_LL_TASKS_PER_PRI_COUNT = 14u,
  LL_PRI_COUNT = 15u,
  MAX_DP_TASKS_COUNT = 16u,
  MAX_LIBS_COUNT = 17u,
  SCHEDULER_CONFIG = 18u,
  XTAL_FREQ_HZ = 19u,
};

enum class HardwareConfigType : uint32_t {
  CAVS_VERSION = 0u,
  DSP_CORES = 1u,
  MEM_PAGE_BYTES = 2u,
  TOTAL_PHYS_MEM_PAGES = 3u,
  I2S_CAPS = 4u,
  GPDMA_CAPS = 5u,
  GATEWAY_COUNT = 6u,
  HP_EBB_COUNT = 7u,
  LP_EBB_COUNT = 8u,
  EBB_SIZE_BYTES = 9u,
};

// Base FW Common

struct TLVHeader {
  uint32_t type;
  uint32_t length;
  uint8_t data[];
} __PACKED;

// Base FW Modules Info

struct SegmentDesc {
  uint32_t flags;
  uint32_t v_base_addr;
  uint32_t file_offset;
} __PACKED;

struct ModuleEntry {
  uint16_t module_id;
  uint16_t state_flags;
  uint8_t name[8];
  uint32_t uuid[4];
  uint32_t type;
  uint8_t hash[32];
  uint32_t entry_point;
  uint16_t cfg_offset;
  uint16_t cfg_count;
  uint16_t affinity_mask;
  uint16_t instance_max_count;
  uint16_t instance_bss_size;
  SegmentDesc segments[3];
  uint8_t reserved[2];  // not in spec but seems necessary
} __PACKED;

struct ModulesInfo {
  uint32_t module_count;
  ModuleEntry module_info[];
} __PACKED;

// Base FW Pipeline List Info

struct PipelineListInfo {
  uint32_t ppl_count;
  uint32_t ppl_id[];
} __PACKED;

// Base FW Pipeline Props

struct ModInstListInfo {
  uint32_t module_instance_count;
  uint32_t module_instance_id[];
};

struct PipelineProps {
  uint32_t id;
  uint32_t priority;
  uint32_t state;
  uint32_t total_memory_bytes;
  uint32_t used_memory_bytes;
  uint32_t context_pages;
  ModInstListInfo module_instances;
  // tasks follow
} __PACKED;

// Copier Module

constexpr uint32_t NODE_ID_DMA_TYPE_MASK = 0x1F;
constexpr uint32_t NODE_ID_DMA_TYPE_SHIFT = 8;
constexpr uint32_t NODE_ID_I2S_INSTANCE_MASK = 0xF;
constexpr uint32_t NODE_ID_I2S_INSTANCE_SHIFT = 4;
constexpr uint32_t NODE_ID_TIME_SLOT_MASK = 0xF;
constexpr uint32_t NODE_ID_DMA_ID_MASK = 0xFF;

constexpr uint8_t DMA_TYPE_HDA_HOST_OUTPUT = 0;
constexpr uint8_t DMA_TYPE_HDA_HOST_INPUT = 1;
constexpr uint8_t DMA_TYPE_I2S_LINK_OUTPUT = 12;
constexpr uint8_t DMA_TYPE_I2S_LINK_INPUT = 13;

struct CopierGatewayCfg {
  uint32_t node_id;
  uint32_t dma_buffer_size;
  uint32_t config_length;
  uint8_t config_data[];
} __PACKED;

#define _SIC_ static inline constexpr

_SIC_ uint32_t HDA_GATEWAY_CFG_NODE_ID(uint8_t dma_type, uint8_t dma_id) {
  return ((dma_type & NODE_ID_DMA_TYPE_MASK) << NODE_ID_DMA_TYPE_SHIFT) |
         (dma_id & NODE_ID_DMA_ID_MASK);
}

_SIC_ uint32_t I2S_GATEWAY_CFG_NODE_ID(uint8_t dma_type, uint8_t i2s_instance, uint8_t time_slot) {
  return ((dma_type & NODE_ID_DMA_TYPE_MASK) << NODE_ID_DMA_TYPE_SHIFT) |
         ((i2s_instance & NODE_ID_I2S_INSTANCE_MASK) << NODE_ID_I2S_INSTANCE_SHIFT) |
         (time_slot & NODE_ID_TIME_SLOT_MASK);
}

#undef _SIC_

struct CopierCfg {
  BaseModuleCfg base_cfg;
  AudioDataFormat out_fmt;
  uint32_t copier_feature_mask;
  CopierGatewayCfg gtw_cfg;
} __PACKED;

}  // namespace intel_hda
}  // namespace audio

#endif  // __cplusplus

#endif  // INTEL_HDA_UTILS_INTEL_AUDIO_DSP_IPC_H_
