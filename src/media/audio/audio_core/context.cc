// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/context.h"

#include "src/media/audio/audio_core/activity_dispatcher.h"
#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_tuner_impl.h"
#include "src/media/audio/audio_core/effects_controller_impl.h"
#include "src/media/audio/audio_core/link_matrix.h"
#include "src/media/audio/audio_core/plug_detector.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_gain_reporter_impl.h"
#include "src/media/audio/audio_core/usage_reporter_impl.h"

namespace media::audio {
namespace {

// All audio renderer buffers will need to fit within this VMAR. We want to choose a size here large
// enough that will accomodate all the mappings required by all clients while also being small
// enough to avoid unnecessary page table fragmentation.
constexpr size_t kAudioRendererVmarSize = 16ull * 1024 * 1024 * 1024;
constexpr zx_vm_option_t kAudioRendererVmarFlags =
    ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

class ContextImpl : public Context {
 public:
  ContextImpl(std::unique_ptr<ThreadingModel> threading_model,
              std::unique_ptr<sys::ComponentContext> component_context,
              std::unique_ptr<PlugDetector> plug_detector, ProcessConfig process_config)
      : threading_model_(std::move(threading_model)),
        component_context_(std::move(component_context)),
        process_config_(std::move(process_config)),
        route_graph_(&link_matrix_),
        device_manager_(*threading_model_, std::move(plug_detector), route_graph_, link_matrix_,
                        process_config_),
        stream_volume_manager_(threading_model_->FidlDomain().dispatcher()),
        audio_admin_(&stream_volume_manager_, threading_model_->FidlDomain().dispatcher(),
                     &usage_reporter_, &activity_dispatcher_),
        vmar_manager_(
            fzl::VmarManager::Create(kAudioRendererVmarSize, nullptr, kAudioRendererVmarFlags)),
        usage_gain_reporter_(this),
        effects_controller_(*this),
        audio_tuner_(*this) {
    FX_DCHECK(vmar_manager_ != nullptr) << "Failed to allocate VMAR";

    zx_status_t res = device_manager_.Init();
    FX_DCHECK(res == ZX_OK);

    auto throttle = ThrottleOutput::Create(threading_model_.get(), &device_manager_, &link_matrix_);
    throttle_output_ = throttle.get();
    route_graph_.SetThrottleOutput(threading_model_.get(), std::move(throttle));
  }

  // Disallow copy & move.
  ContextImpl(ContextImpl&& o) = delete;
  ContextImpl& operator=(ContextImpl&& o) = delete;
  ContextImpl(const ContextImpl&) = delete;
  ContextImpl& operator=(const ContextImpl&) = delete;

  ~ContextImpl() override = default;

  // |fuchsia::audio::Context|
  void PublishOutgoingServices() override {
    component_context_->outgoing()->AddPublicService(device_manager_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(usage_reporter_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(activity_dispatcher_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(usage_gain_reporter_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(audio_tuner_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(effects_controller_.GetFidlRequestHandler());
  }
  ThreadingModel& threading_model() override { return *threading_model_; }
  AudioDeviceManager& device_manager() override { return device_manager_; }
  AudioAdmin& audio_admin() override { return audio_admin_; }
  fbl::RefPtr<fzl::VmarManager> vmar() const override { return vmar_manager_; }
  StreamVolumeManager& volume_manager() override { return stream_volume_manager_; }
  RouteGraph& route_graph() override { return route_graph_; }
  LinkMatrix& link_matrix() override { return link_matrix_; }
  const ProcessConfig& process_config() const override { return process_config_; }
  sys::ComponentContext& component_context() override { return *component_context_; }
  AudioOutput* throttle_output() const override { return throttle_output_; }

 private:
  std::unique_ptr<ThreadingModel> threading_model_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  ProcessConfig process_config_;

  AudioOutput* throttle_output_;

  LinkMatrix link_matrix_;
  RouteGraph route_graph_;

  // State for dealing with devices.
  AudioDeviceManager device_manager_;

  // Router for volume changes.
  StreamVolumeManager stream_volume_manager_;

  UsageReporterImpl usage_reporter_;

  // Dispatcher for audio activity.
  ActivityDispatcherImpl activity_dispatcher_;

  // Audio usage manager
  AudioAdmin audio_admin_;

  // We allocate a sub-vmar to hold the audio renderer buffers. Keeping these in a sub-vmar allows
  // us to take advantage of ASLR while minimizing page table fragmentation.
  fbl::RefPtr<fzl::VmarManager> vmar_manager_;

  UsageGainReporterImpl usage_gain_reporter_;

  EffectsControllerImpl effects_controller_;
  AudioTunerImpl audio_tuner_;
};

}  // namespace

std::unique_ptr<Context> Context::Create(std::unique_ptr<ThreadingModel> threading_model,
                                         std::unique_ptr<sys::ComponentContext> component_context,
                                         std::unique_ptr<PlugDetector> plug_detector,
                                         ProcessConfig process_config) {
  return std::make_unique<ContextImpl>(std::move(threading_model), std::move(component_context),
                                       std::move(plug_detector), std::move(process_config));
}

}  // namespace media::audio
