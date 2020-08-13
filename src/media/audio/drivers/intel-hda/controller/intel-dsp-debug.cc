// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp.h"

namespace audio {
namespace intel_hda {

void IntelDsp::DumpRegs() {
  LOG(INFO, "ADSP registers\n");
  LOG(INFO, "ADSPCS   0x%08x\n", REG_RD(&regs()->adspcs));
  LOG(INFO, "ADSPIC   0x%08x\n", REG_RD(&regs()->adspic));
  LOG(INFO, "ADSPIS   0x%08x\n", REG_RD(&regs()->adspis));
  LOG(INFO, "ADSPIC2  0x%08x\n", REG_RD(&regs()->adspic2));
  LOG(INFO, "ADSPIS2  0x%08x\n", REG_RD(&regs()->adspis2));
  LOG(INFO, "HIPCT    0x%08x\n", REG_RD(&regs()->hipct));
  LOG(INFO, "HIPCTE   0x%08x\n", REG_RD(&regs()->hipcte));
  LOG(INFO, "HIPCI    0x%08x\n", REG_RD(&regs()->hipci));
  LOG(INFO, "HIPCIE   0x%08x\n", REG_RD(&regs()->hipcie));
  LOG(INFO, "HIPCCTL  0x%08x\n", REG_RD(&regs()->hipcctl));
  LOG(INFO, "Code Loader registers\n");
  LOG(INFO, "CTL_STS  0x%08x\n", REG_RD(&regs()->cldma.stream.ctl_sts.w));
  LOG(INFO, "CBL      0x%08x\n", REG_RD(&regs()->cldma.stream.cbl));
  LOG(INFO, "LVI      0x%08x\n", REG_RD(&regs()->cldma.stream.lvi));
  LOG(INFO, "FIFOD    0x%08x\n", REG_RD(&regs()->cldma.stream.fifod));
  LOG(INFO, "FMT      0x%08x\n", REG_RD(&regs()->cldma.stream.fmt));
  LOG(INFO, "BDPL     0x%08x\n", REG_RD(&regs()->cldma.stream.bdpl));
  LOG(INFO, "BDPU     0x%08x\n", REG_RD(&regs()->cldma.stream.bdpu));
  LOG(INFO, "SPBFCH   0x%08x\n", REG_RD(&regs()->cldma.spbfch));
  LOG(INFO, "SPBFCTL  0x%08x\n", REG_RD(&regs()->cldma.spbfctl));
  LOG(INFO, "SPIB     0x%08x\n", REG_RD(&regs()->cldma.spib));
  LOG(INFO, "MAXFIFOS 0x%08x\n", REG_RD(&regs()->cldma.maxfifos));
  LOG(INFO, "Firmware registers\n");
  LOG(INFO, "FW_STATUS     0x%08x\n", REG_RD(&fw_regs()->fw_status));
  LOG(INFO, "ERROR_CODE    0x%08x\n", REG_RD(&fw_regs()->error_code));
  LOG(INFO, "FW_PWR_STATUS 0x%08x\n", REG_RD(&fw_regs()->fw_pwr_status));
  LOG(INFO, "ROM_INFO      0x%08x\n", REG_RD(&fw_regs()->rom_info));
}

void IntelDsp::DumpFirmwareConfig(const TLVHeader* config, size_t length) {
  LOG(INFO, "===== Firmware Config =====\n");
  size_t bytes = 0;
  while (bytes < length) {
    if (length - bytes <= sizeof(*config)) {
      LOG(ERROR, "Got short firmware config TLV header\n");
      return;
    }

    auto ptr = reinterpret_cast<const uint8_t*>(config);
    auto cfg = reinterpret_cast<const TLVHeader*>(ptr + bytes);
    auto type = static_cast<FirmwareConfigType>(cfg->type);

    if ((cfg->length + sizeof(*cfg)) > (bytes - length)) {
      LOG(ERROR, "Got short firmware config TLV entry\n");
      return;
    }

    // Values dumped below are all uint32_t
    static constexpr size_t PAYLOAD_LENGTH = sizeof(uint32_t);
    if (cfg->length < PAYLOAD_LENGTH) {
      LOG(ERROR, "Got short firmware config payload length (got %u expected %zu)\n", cfg->length,
          PAYLOAD_LENGTH);
      continue;
    }

    switch (type) {
      case FirmwareConfigType::FW_VERSION: {
        auto version = reinterpret_cast<const uint16_t*>(cfg->data);
        LOG(INFO, "                fw_version: %hu.%hu hotfix %hu (build %hu)\n", version[0],
            version[1], version[2], version[3]);
        break;
      }
      case FirmwareConfigType::MEMORY_RECLAIMED: {
        auto memory = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "          memory_reclaimed: %u\n", *memory);
        break;
      }
      case FirmwareConfigType::SLOW_CLOCK_FREQ_HZ: {
        auto freq = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "                  osc_freq: %u\n", *freq);
        break;
      }
      case FirmwareConfigType::FAST_CLOCK_FREQ_HZ: {
        auto freq = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "                  pll_freq: %u\n", *freq);
        break;
      }
      case FirmwareConfigType::DMA_BUFFER_CONFIG: {
        auto buf_cfg = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "             dma_buf_count: %u\n", cfg->length / 8);
        for (uint32_t i = 0; i < cfg->length / 8; i++) {
          LOG(INFO, "          dma_min_size[%02u]: %u\n", i, buf_cfg[i * 2]);
          LOG(INFO, "          dma_max_size[%02u]: %u\n", i, buf_cfg[(i * 2) + 1]);
        }
        break;
      }
      case FirmwareConfigType::ALH_SUPPORT_LEVEL: {
        auto level = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "         alh_support_level: %u\n", *level);
        break;
      }
      case FirmwareConfigType::IPC_DL_MAILBOX_BYTES: {
        auto bytes = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "           mailbox_in_size: %u\n", *bytes);
        break;
      }
      case FirmwareConfigType::IPC_UL_MAILBOX_BYTES: {
        auto bytes = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "          mailbox_out_size: %u\n", *bytes);
        break;
      }
      case FirmwareConfigType::TRACE_LOG_BYTES: {
        auto bytes = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "            trace_log_size: %u\n", *bytes);
        break;
      }
      case FirmwareConfigType::MAX_PPL_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "             max_ppl_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_ASTATE_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "          max_astate_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_MODULE_PIN_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "      max_module_pin_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MODULES_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "             modules_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_MOD_INST_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "        max_mod_inst_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_LL_TASKS_PER_PRI_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "max_ll_tasks_per_pri_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::LL_PRI_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "              ll_pri_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_DP_TASKS_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "        max_dp_tasks_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::MAX_LIBS_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "            max_libs_count: %u\n", *count);
        break;
      }
      case FirmwareConfigType::SCHEDULER_CONFIG: {
        // Skip dumping this one
        break;
      }
      case FirmwareConfigType::XTAL_FREQ_HZ: {
        auto freq = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "              xtal_freq_hz: %u\n", *freq);
        break;
      }
      default:
        LOG(ERROR, "Unknown firmware config type %u\n", cfg->type);
        break;
    }
    bytes += sizeof(*cfg) + cfg->length;
  }
}

void IntelDsp::DumpHardwareConfig(const TLVHeader* config, size_t length) {
  LOG(INFO, "===== Hardware Config =====\n");
  size_t bytes = 0;
  while (bytes < length) {
    if (length - bytes <= sizeof(*config)) {
      LOG(ERROR, "Got short hardware config TLV header\n");
      return;
    }

    auto ptr = reinterpret_cast<const uint8_t*>(config);
    auto cfg = reinterpret_cast<const TLVHeader*>(ptr + bytes);
    auto type = static_cast<HardwareConfigType>(cfg->type);

    if ((cfg->length + sizeof(*cfg)) > (bytes - length)) {
      LOG(ERROR, "Got short hardware config TLV entry\n");
      return;
    }

    // Values dumped below are all uint32_t
    static constexpr size_t PAYLOAD_LENGTH = sizeof(uint32_t);
    if (cfg->length < PAYLOAD_LENGTH) {
      LOG(ERROR, "Got short hardware config payload length (got %u expected %zu)\n", cfg->length,
          PAYLOAD_LENGTH);
      continue;
    }

    switch (type) {
      case HardwareConfigType::CAVS_VERSION: {
        auto version = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "        cavs_version: 0x%08x\n", *version);
        break;
      }
      case HardwareConfigType::DSP_CORES: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "           dsp_cores: %u\n", *count);
        break;
      }
      case HardwareConfigType::MEM_PAGE_BYTES: {
        auto bytes = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "      mem_page_bytes: %u\n", *bytes);
        break;
      }
      case HardwareConfigType::TOTAL_PHYS_MEM_PAGES: {
        auto pages = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "total_phys_mem_pages: %u\n", *pages);
        break;
      }
      case HardwareConfigType::I2S_CAPS: {
        // Skip dumping this one
        break;
      }
      case HardwareConfigType::GPDMA_CAPS: {
        // Skip dumping this one
        break;
      }
      case HardwareConfigType::GATEWAY_COUNT: {
        auto count = reinterpret_cast<const uint32_t*>(cfg->data);
        LOG(INFO, "       gateway_count: %u\n", *count);
        break;
      }
      case HardwareConfigType::HP_EBB_COUNT: {
        // Skip dumping this one
        break;
      }
      case HardwareConfigType::LP_EBB_COUNT: {
        // Skip dumping this one
        break;
      }
      case HardwareConfigType::EBB_SIZE_BYTES: {
        // Skip dumping this one
        break;
      }
      default:
        LOG(ERROR, "Unknown hardware config type %u\n", cfg->type);
        break;
    }
    bytes += sizeof(*cfg) + cfg->length;
  }
}

void IntelDsp::DumpModulesInfo(const ModuleEntry* info, uint32_t count) {
  LOG(INFO, "num modules: %u\n", count);
  for (uint32_t i = 0; i < count; i++) {
    LOG(INFO, "[%02u]:\n", i);
    LOG(INFO, "  module_id: %u\n", info[i].module_id);
    LOG(INFO, "  state_flags: 0x%04x\n", info[i].state_flags);
    char name[9] = {0};
    strncpy(name, reinterpret_cast<const char*>(info[i].name), sizeof(name) - 1);
    LOG(INFO, "         name: %s\n", name);
    LOG(INFO, "         uuid: %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n", info[i].uuid[0],
        info[i].uuid[1] & 0xFFFF, info[i].uuid[1] >> 16, info[i].uuid[2] & 0xFF,
        (info[i].uuid[2] >> 8) & 0xFF, (info[i].uuid[2] >> 16) & 0xFF,
        (info[i].uuid[2] >> 24) & 0xFF, info[i].uuid[3] & 0xFF, (info[i].uuid[3] >> 8) & 0xFF,
        (info[i].uuid[3] >> 16) & 0xFF, (info[i].uuid[3] >> 24) & 0xFF);
  }
}

void IntelDsp::DumpPipelineListInfo(const PipelineListInfo* info) {
  LOG(INFO, "num pipelines: %u\n", info->ppl_count);
  for (uint32_t i = 0; i < info->ppl_count; i++) {
    LOG(INFO, "[%02u]: id %u\n", i, info->ppl_id[i]);
  }
}

void IntelDsp::DumpPipelineProps(const PipelineProps* props) {
  LOG(INFO, "                   id: %u\n", props->id);
  LOG(INFO, "             priority: %u\n", props->priority);
  LOG(INFO, "                state: %u\n", props->state);
  LOG(INFO, "   total_memory_bytes: %u\n", props->total_memory_bytes);
  LOG(INFO, "    used_memory_bytes: %u\n", props->used_memory_bytes);
  LOG(INFO, "        context_pages: %u\n", props->context_pages);
  LOG(INFO, "module_instance_count: %u\n", props->module_instances.module_instance_count);
  for (uint32_t i = 0; i < props->module_instances.module_instance_count; i++) {
    LOG(INFO, " module_instance[%1u]: id 0x%08x\n", i,
        props->module_instances.module_instance_id[i]);
  }
}

}  // namespace intel_hda
}  // namespace audio
