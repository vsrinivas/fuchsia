// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-modules.h"

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <map>
#include <unordered_map>

#include <fbl/string_printf.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "binary_decoder.h"
#include "debug-logging.h"
#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

// Maximum number of instances of a particular module or pipelines we
// will allocate before producing an error.
//
// In practice, the DSP will likely fail creation far before we reach
// this number.
constexpr int kMaxInstancesPerModule = 255;
constexpr int kMaxPipelines = 255;

namespace {
zx_status_t LargeConfigGet(DspChannel* ipc, uint16_t module_id, uint8_t instance_id,
                           BaseFWParamType large_param_id, cpp20::span<uint8_t> buffer,
                           size_t* bytes_received) {
  GLOBAL_LOG(TRACE, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u)", module_id, instance_id,
             to_underlying(large_param_id));

  if (buffer.size_bytes() > IPC_EXT_DATA_OFF_MAX_SIZE) {
    buffer = buffer.subspan(0, IPC_EXT_DATA_OFF_MAX_SIZE);
  }

  size_t bytes_received_local;
  zx::result result =
      ipc->SendWithData(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                ModuleMsgType::LARGE_CONFIG_GET, instance_id, module_id),
                        IPC_LARGE_CONFIG_EXT(true, false, to_underlying(large_param_id),
                                             static_cast<uint32_t>(buffer.size())),
                        cpp20::span<const uint8_t>(), buffer, &bytes_received_local);
  if (!result.is_ok()) {
    GLOBAL_LOG(ERROR, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) failed :%s", module_id,
               instance_id, to_underlying(large_param_id), result.status_string());
    return result.status_value();
  }

  GLOBAL_LOG(TRACE,
             "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) success: received %ld byte(s).",
             module_id, instance_id, to_underlying(large_param_id), bytes_received_local);
  if (bytes_received != nullptr) {
    *bytes_received = bytes_received_local;
  }
  return ZX_OK;
}
}  // namespace

// Parse the module list returned from the DSP.
zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> ParseModules(
    cpp20::span<const uint8_t> data) {
  BinaryDecoder decoder(data);

  // Parse returned module information.
  auto header = decoder.Read<ModulesInfo>();
  if (!header.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not read DSP module information");
    return zx::error(header.status_value());
  }

  // Read modules.
  uint32_t count = header.value().module_count;
  std::map<fbl::String, std::unique_ptr<ModuleEntry>> modules;
  for (uint32_t i = 0; i < count; i++) {
    // Parse the next module.
    auto entry = std::make_unique<ModuleEntry>();
    zx_status_t status = decoder.Read<ModuleEntry>(entry.get());
    if (status != ZX_OK) {
      GLOBAL_LOG(ERROR, "Could not read module entry");
      return zx::error(status);
    }

    // Add it to the dictionary, ensuring it is not already there.
    fbl::String name = ParseUnpaddedString(entry->name);
    auto [_, success] = modules.insert({name, std::move(entry)});
    if (!success) {
      GLOBAL_LOG(ERROR, "Duplicate module name: '%s'.", name.c_str());
      return zx::error(ZX_ERR_INTERNAL);
    }
  }

  return zx::ok(std::move(modules));
}

DspModuleController::DspModuleController(DspChannel* channel) : channel_(channel) {}

// Create an instance of the module "type".
//
// Returns the ID of the created module on success.
zx::result<DspModuleId> DspModuleController::CreateModule(DspModuleType type,
                                                          DspPipelineId parent_pipeline,
                                                          ProcDomain scheduling_domain,
                                                          cpp20::span<const uint8_t> data) {
  // Ensure data is not too large or non-word sized.
  if ((data.size() % kIpcInitInstanceExtBytesPerWord) ||
      data.size() >= std::numeric_limits<uint16_t>::max()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Allocate an ID.
  zx::result<uint8_t> instance_id = AllocateInstanceId(type);
  if (!instance_id.is_ok()) {
    return zx::error(instance_id.status_value());
  }
  GLOBAL_LOG(TRACE, "CreateModule(type %u, inst %u)", type, instance_id.value());

  // Create the module.
  zx::result result = channel_->SendWithData(
      IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::INIT_INSTANCE,
              instance_id.value(), type),
      IPC_INIT_INSTANCE_EXT(scheduling_domain, /*core_id=*/0, parent_pipeline.id,
                            static_cast<uint16_t>((data.size()) / kIpcInitInstanceExtBytesPerWord)),
      data, cpp20::span<uint8_t>(), nullptr);
  if (!result.is_ok()) {
    GLOBAL_LOG(TRACE, "CreateModule failed: %s", result.status_string());
    return zx::error(result.status_value());
  }
  return zx::ok(DspModuleId{/*type=*/type, /*id=*/instance_id.value()});
}

// Create a pipeline.
//
// Return ths ID of the created pipeline on success.
zx::result<DspPipelineId> DspModuleController::CreatePipeline(uint8_t priority,
                                                              uint16_t memory_pages,
                                                              bool low_power) {
  // Allocate a pipeline name.
  if (pipelines_allocated_ >= kMaxPipelines) {
    GLOBAL_LOG(ERROR, "Too many pipelines created.");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }
  uint8_t id = pipelines_allocated_++;
  GLOBAL_LOG(TRACE, "CreatePipeline(inst %u)", id);

  // Create the pipeline.
  zx::result result = channel_->Send(IPC_CREATE_PIPELINE_PRI(id, priority, memory_pages),
                                     IPC_CREATE_PIPELINE_EXT(low_power));
  if (!result.is_ok()) {
    GLOBAL_LOG(TRACE, "CreatePipeline failed: %s", result.status_string());
    zx::error(result.status_value());
  }
  return zx::ok(DspPipelineId{id});
}

// Connect an output pin of one module to the input pin of another.
zx::result<> DspModuleController::BindModules(DspModuleId source_module, uint8_t src_output_pin,
                                              DspModuleId dest_module, uint8_t dest_input_pin) {
  GLOBAL_LOG(TRACE, "BindModules (mod %u inst %u):%u --> (mod %u, inst %u):%u", source_module.type,
             source_module.id, src_output_pin, dest_module.type, dest_module.id, dest_input_pin);

  zx::result result = channel_->Send(
      IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::BIND, source_module.id,
              source_module.type),
      IPC_BIND_UNBIND_EXT(dest_module.type, dest_module.id, dest_input_pin, src_output_pin));
  if (!result.is_ok()) {
    GLOBAL_LOG(TRACE, "BindModules failed: %s", result.status_string());
    return zx::error(result.status_value());
  }

  return zx::ok();
}

// Enable/disable the given pipeline.
zx::result<> DspModuleController::SetPipelineState(DspPipelineId pipeline, PipelineState state,
                                                   bool sync_stop_start) {
  GLOBAL_LOG(TRACE, "SetPipelineStatus(pipeline=%u, state=%u, sync_stop_start=%s)", pipeline.id,
             static_cast<unsigned int>(state), sync_stop_start ? "true" : "false");

  zx::result result = channel_->Send(IPC_SET_PIPELINE_STATE_PRI(pipeline.id, state),
                                     IPC_SET_PIPELINE_STATE_EXT(false, sync_stop_start));
  if (!result.is_ok()) {
    GLOBAL_LOG(TRACE, "SetPipelineStatus failed: %s", result.status_string());
    return zx::error(result.status_value());
  }

  return zx::ok();
}

zx::result<uint8_t> DspModuleController::AllocateInstanceId(DspModuleType type) {
  uint8_t& instance_count = allocated_instances_[type];
  if (instance_count >= kMaxInstancesPerModule) {
    GLOBAL_LOG(ERROR, "Could not allocate more instances of given module type.");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }
  uint8_t result = instance_count;
  instance_count++;
  return zx::ok(std::move(result));
}

zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>>
DspModuleController::ReadModuleDetails() {
  constexpr int kMaxModules = 64;
  uint8_t buffer[sizeof(ModulesInfo) + (kMaxModules * sizeof(ModuleEntry))];
  cpp20::span data{buffer};

  // Fetch module information.
  size_t bytes_received;
  zx_status_t result =
      LargeConfigGet(channel_, /*module_id=*/0, /*instance_id=*/0,
                     /*large_param_id=*/BaseFWParamType::MODULES_INFO, data, &bytes_received);
  if (result != ZX_OK) {
    GLOBAL_LOG(ERROR, "Failed to fetch module information from DSP");
    return zx::error(result);
  }

  // Parse DSP's module list.
  zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> modules =
      ParseModules(data.subspan(0, bytes_received));
  if (!modules.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not parse DSP's module list");
    return zx::error(modules.status_value());
  }

  // If tracing is enabled, print basic module information.
  if (zxlog_level_enabled(DEBUG)) {
    GLOBAL_LOG(DEBUG, "DSP firmware has %ld module(s) configured.", modules.value().size());
    for (const auto& elem : modules.value()) {
      GLOBAL_LOG(DEBUG, "  module %s (id=%d)", elem.first.c_str(), elem.second->module_id);
    }
  }

  return zx::ok(std::move(modules.value()));
}

zx::result<DspPipelineId> CreateSimplePipeline(DspModuleController* controller,
                                               std::initializer_list<DspModule> modules) {
  // Create a pipeline.
  //
  // TODO(fxbug.dev/31426): Calculate actual memory usage.
  const uint16_t pipeline_memory_pages_needed = 4;
  zx::result<DspPipelineId> pipeline =
      controller->CreatePipeline(/*pipeline_priority=*/0,
                                 /*pipeline_memory_pages=*/pipeline_memory_pages_needed,
                                 /*low_power=*/true);
  if (!pipeline.is_ok()) {
    GLOBAL_LOG(ERROR, "Could not create pipeline");
    zx::error(pipeline.status_value());
  }
  DspPipelineId pipeline_id = std::move(pipeline.value());

  // Create the modules.
  int module_count = 0;
  DspModuleId prev_module;
  for (const DspModule& module : modules) {
    // Create the module.
    zx::result<DspModuleId> id =
        controller->CreateModule(module.type, pipeline_id, ProcDomain::LOW_LATENCY, module.data);
    if (!id.is_ok()) {
      GLOBAL_LOG(ERROR, "Failed creating module #%u.", module_count);
      return zx::error(id.status_value());
    }

    // Join it to the previous module.
    if (module_count > 0) {
      zx::result result = controller->BindModules(prev_module, /*src_output_pin=*/0, id.value(),
                                                  /*dest_input_pin=*/0);
      if (!result.is_ok()) {
        GLOBAL_LOG(ERROR, "Failed to connect module #%u to #%u", module_count - 1, module_count);
        return zx::error(result.status_value());
      }
    }

    prev_module = id.value();
    module_count++;
  }

  return zx::ok(std::move(pipeline_id));
}

}  // namespace intel_hda
}  // namespace audio
