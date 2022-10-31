// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/context.h"

#include "src/media/audio/audio_core/activity_dispatcher.h"
#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_impl.h"
#include "src/media/audio/audio_core/audio_tuner_impl.h"
#include "src/media/audio/audio_core/effects_controller_impl.h"
#include "src/media/audio/audio_core/idle_policy.h"
#include "src/media/audio/audio_core/link_matrix.h"
#include "src/media/audio/audio_core/plug_detector.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_gain_reporter_impl.h"
#include "src/media/audio/audio_core/usage_reporter_impl.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"

namespace media::audio {
namespace {

std::unique_ptr<EffectsLoaderV2> CreateEffectsLoaderV2(
    const sys::ComponentContext& component_context) {
  auto result = EffectsLoaderV2::CreateFromContext(component_context);
  if (result.is_ok()) {
    return std::move(result.value());
  }

  FX_PLOGS(WARNING, result.error())
      << "Failed to connect to V2 effects factory: V2 effects are not available";
  return nullptr;
}

// All audio renderer buffers will need to fit within this VMAR. We want to choose a size here large
// enough that will accommodate all the mappings required by all clients while also being small
// enough to avoid unnecessary page table fragmentation.
constexpr size_t kAudioRendererVmarSize = 16ull * 1024 * 1024 * 1024;
constexpr zx_vm_option_t kAudioRendererVmarFlags =
    ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

class ContextImpl : public Context {
 public:
  ContextImpl(std::unique_ptr<ThreadingModel> threading_model,
              std::unique_ptr<sys::ComponentContext> component_context,
              std::unique_ptr<PlugDetector> plug_detector, ProcessConfig process_config,
              std::shared_ptr<AudioCoreClockFactory> clock_factory)
      : threading_model_(std::move(threading_model)),
        component_context_(std::move(component_context)),
        process_config_(std::move(process_config)),
        route_graph_(&link_matrix_),
        effects_loader_v2_(CreateEffectsLoaderV2(*component_context_)),
        clock_factory_(clock_factory),
        idle_policy_(this),
        device_manager_(*threading_model_, std::move(plug_detector), link_matrix_, process_config_,
                        clock_factory_, idle_policy_, effects_loader_v2_.get()),
        stream_volume_manager_(threading_model_->FidlDomain().dispatcher()),
        audio_admin_(&stream_volume_manager_, &usage_reporter_, &activity_dispatcher_,
                     &idle_policy_, threading_model_->FidlDomain().dispatcher()),
        vmar_manager_(
            fzl::VmarManager::Create(kAudioRendererVmarSize, nullptr, kAudioRendererVmarFlags)),
        usage_gain_reporter_(this),
        effects_controller_(*this),
        audio_tuner_(*this),
        audio_(this) {
    FX_DCHECK(vmar_manager_ != nullptr) << "Failed to allocate VMAR";
    zx_status_t res = device_manager_.Init();
    FX_DCHECK(res == ZX_OK);

    // We call Reporter::InitializeSingleton before Context::Create. Reporter is now safe to use.
    Reporter::Singleton().SetNumThermalStates(process_config_.thermal_config().states().size());

    auto throttle = ThrottleOutput::Create(process_config_.device_config(), threading_model_.get(),
                                           &device_manager_, &link_matrix_, clock_factory_);
    throttle_output_ = throttle.get();
    route_graph_.SetThrottleOutput(threading_model_.get(), std::move(throttle));

    effects_controller_client_ =
        component_context_->svc()->Connect<fuchsia::media::audio::EffectsController>();
    effects_controller_client_.set_error_handler([](zx_status_t status) {
      FX_PLOGS(ERROR, status) << "Connection to fuchsia.media.audio.EffectsController failed: ";
    });
  }

  // Disallow copy & move.
  ContextImpl(ContextImpl&& o) = delete;
  ContextImpl& operator=(ContextImpl&& o) = delete;
  ContextImpl(const ContextImpl&) = delete;
  ContextImpl& operator=(const ContextImpl&) = delete;

  ~ContextImpl() override = default;

  // |fuchsia::audio::Context|
  void PublishOutgoingServices() override {
    component_context_->outgoing()->AddPublicService(audio_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(device_manager_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(usage_reporter_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(activity_dispatcher_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(usage_gain_reporter_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(audio_tuner_.GetFidlRequestHandler());
    component_context_->outgoing()->AddPublicService(effects_controller_.GetFidlRequestHandler());
  }
  ThreadingModel& threading_model() override { return *threading_model_; }
  std::shared_ptr<AudioCoreClockFactory> clock_factory() override { return clock_factory_; }
  AudioDeviceManager& device_manager() override { return device_manager_; }
  AudioAdmin& audio_admin() override { return audio_admin_; }
  fbl::RefPtr<fzl::VmarManager> vmar() const override { return vmar_manager_; }
  StreamVolumeManager& volume_manager() override { return stream_volume_manager_; }
  RouteGraph& route_graph() override { return route_graph_; }
  LinkMatrix& link_matrix() override { return link_matrix_; }
  const ProcessConfig& process_config() const override { return process_config_; }
  sys::ComponentContext& component_context() override { return *component_context_; }
  AudioOutput* throttle_output() const override { return throttle_output_; }
  DeviceRouter& device_router() override { return idle_policy_; }
  ActiveStreamCountReporter& active_stream_count_reporter() override { return idle_policy_; }
  fuchsia::media::audio::EffectsControllerPtr& effects_controller() override {
    return effects_controller_client_;
  }
  EffectsLoaderV2& effects_loader_v2() override { return *effects_loader_v2_; }

 private:
  std::unique_ptr<ThreadingModel> threading_model_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  ProcessConfig process_config_;

  AudioOutput* throttle_output_;

  LinkMatrix link_matrix_;
  RouteGraph route_graph_;
  std::unique_ptr<EffectsLoaderV2> effects_loader_v2_;

  // Manages clock creation.
  std::shared_ptr<AudioCoreClockFactory> clock_factory_;

  IdlePolicy idle_policy_;

  // State for dealing with devices.
  AudioDeviceManager device_manager_;

  // Router for volume changes.
  StreamVolumeManager stream_volume_manager_;

  UsageReporterImpl usage_reporter_;

  // Dispatcher for audio activity.
  ActivityDispatcherImpl activity_dispatcher_;

  // Audio usage and output-idle policy manager
  AudioAdmin audio_admin_;

  // We allocate a sub-vmar to hold the audio renderer buffers. Keeping these in a sub-vmar allows
  // us to take advantage of ASLR while minimizing page table fragmentation.
  fbl::RefPtr<fzl::VmarManager> vmar_manager_;

  UsageGainReporterImpl usage_gain_reporter_;

  EffectsControllerImpl effects_controller_;
  fuchsia::media::audio::EffectsControllerPtr effects_controller_client_;

  AudioTunerImpl audio_tuner_;

  // FIDL service that forwards requests to AudioCore.
  AudioImpl audio_;
};

}  // namespace

std::unique_ptr<Context> Context::Create(std::unique_ptr<ThreadingModel> threading_model,
                                         std::unique_ptr<sys::ComponentContext> component_context,
                                         std::unique_ptr<PlugDetector> plug_detector,
                                         ProcessConfig process_config,
                                         std::shared_ptr<AudioCoreClockFactory> clock_factory) {
  return std::make_unique<ContextImpl>(std::move(threading_model), std::move(component_context),
                                       std::move(plug_detector), std::move(process_config),
                                       std::move(clock_factory));
}

}  // namespace media::audio
