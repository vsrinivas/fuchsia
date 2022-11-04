// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CONTEXT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CONTEXT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>

#include "src/media/audio/audio_core/v1/active_stream_count_reporter.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/device_registry.h"
#include "src/media/audio/audio_core/v1/process_config.h"
#include "src/media/audio/audio_core/v1/threading_model.h"

namespace media::audio {

class AudioAdmin;
class AudioDeviceManager;
class AudioOutput;
class LinkMatrix;
class PlugDetector;
class ProcessConfig;
class RouteGraph;
class StreamVolumeManager;
class ThreadingModel;
class UsageReporterImpl;
class EffectsLoaderV2;

class Context {
 public:
  static std::unique_ptr<Context> Create(std::unique_ptr<ThreadingModel> threading_model,
                                         std::unique_ptr<sys::ComponentContext> component_context,
                                         std::unique_ptr<PlugDetector> plug_detector,
                                         ProcessConfig process_config,
                                         std::shared_ptr<AudioCoreClockFactory> clock_factory);

  // Disallow copy & move.
  Context(Context&& o) = delete;
  Context& operator=(Context&& o) = delete;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  virtual ~Context() = default;

  virtual void PublishOutgoingServices() = 0;

  virtual ThreadingModel& threading_model() = 0;
  virtual std::shared_ptr<AudioCoreClockFactory> clock_factory() = 0;
  virtual AudioDeviceManager& device_manager() = 0;
  virtual AudioAdmin& audio_admin() = 0;
  virtual fbl::RefPtr<fzl::VmarManager> vmar() const = 0;
  virtual StreamVolumeManager& volume_manager() = 0;
  virtual RouteGraph& route_graph() = 0;
  virtual LinkMatrix& link_matrix() = 0;
  virtual const ProcessConfig& process_config() const = 0;
  virtual sys::ComponentContext& component_context() = 0;
  virtual AudioOutput* throttle_output() const = 0;
  virtual DeviceRouter& device_router() = 0;
  virtual ActiveStreamCountReporter& active_stream_count_reporter() = 0;
  virtual fuchsia::media::audio::EffectsControllerPtr& effects_controller() = 0;
  virtual EffectsLoaderV2& effects_loader_v2() = 0;

 protected:
  Context() = default;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CONTEXT_H_
