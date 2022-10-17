// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_MODULES_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_MODULES_H_

#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include <intel-hda/utils/intel-audio-dsp-ipc.h>

#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

// Module type.
using DspModuleType = uint16_t;

// Name of a module instance.
struct DspModuleId {
  DspModuleType type;  // Type of the module.
  uint8_t id;          // Instance number of the module.
};

// Name of a pipeline instance.
struct DspPipelineId {
  uint8_t id;
};

// Information about a DSP module instance.
struct DspModule {
  DspModuleType type;
  std::vector<uint8_t> data;
};

// DspModuleController manages set up of modules and pipelines, pipeline states,
// and module/pipeline ID allocation.
//
// Thread compatible.
class DspModuleController {
 public:
  DspModuleController(DspChannel* ipc);

  // Create a pipeline.
  //
  // Return ths ID of the created pipeline on success.
  zx::result<DspPipelineId> CreatePipeline(uint8_t priority, uint16_t memory_pages, bool low_power);

  // Create an instance of the module "type" in the given pipeline.
  //
  // Returns the ID of the created module on success.
  zx::result<DspModuleId> CreateModule(DspModuleType type, DspPipelineId parent_pipeline,
                                       ProcDomain scheduling_domain,
                                       cpp20::span<const uint8_t> data);

  // Connect an output pin of one module to the input pin of another.
  zx::result<> BindModules(DspModuleId source_module, uint8_t src_output_pin,
                           DspModuleId dest_module, uint8_t dest_input_pin);

  // Enable/disable the given pipeline.
  zx::result<> SetPipelineState(DspPipelineId pipeline, PipelineState state, bool sync_stop_start);

  // Fetch details about modules available on the DSP.
  zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> ReadModuleDetails();

 private:
  // Allocate an instance ID for module of type |type|.
  zx::result<uint8_t> AllocateInstanceId(DspModuleType type);

  // Number of instances of each module type that have been created.
  std::unordered_map<DspModuleType, uint8_t> allocated_instances_;

  // Number of pipelines created.
  uint8_t pipelines_allocated_ = 0;

  // Connection to the DSP. Owned elsewhere.
  DspChannel* channel_;
};

// Construct a simple pipeline, consisting of a series of modules in
// a straight line:
//
//    A --> B --> C --> D
//
// Modules should be listed in source to sink order. Each module will be
// joined to the previous module, connecting output pin 0 to input pin 0.
zx::result<DspPipelineId> CreateSimplePipeline(DspModuleController* controller,
                                               std::initializer_list<DspModule> modules);

// Exposed for testing.
zx::result<std::map<fbl::String, std::unique_ptr<ModuleEntry>>> ParseModules(
    cpp20::span<const uint8_t> data);

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_MODULES_H_
