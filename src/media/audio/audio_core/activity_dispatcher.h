// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_ACTIVITY_DISPATCHER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_ACTIVITY_DISPATCHER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <bitset>

#include "src/media/audio/audio_core/audio_admin.h"

namespace media::audio {
class ActivityDispatcherImpl : public AudioAdmin::ActivityDispatcher {
 public:
  ActivityDispatcherImpl();
  ~ActivityDispatcherImpl() override;

  ActivityDispatcherImpl(const ActivityDispatcherImpl&) = delete;
  ActivityDispatcherImpl& operator=(const ActivityDispatcherImpl) = delete;

  // Exposes the request handler for ActivityReporter.
  fidl::InterfaceRequestHandler<fuchsia::media::ActivityReporter> GetFidlRequestHandler();

  // Notifies all of the connected clients that the activity has changed.
  void OnRenderActivityChanged(RenderActivity activity) override;

 private:
  // fuchsia::media::ActivityReporter implementation, associated with a single client.
  class ActivityReporterImpl;

  // Binds a new request to the underlying binding set.
  void Bind(fidl::InterfaceRequest<fuchsia::media::ActivityReporter> request);

  // Last activity observerd by the dispatcher.
  RenderActivity last_known_render_activity_;

  fidl::BindingSet<fuchsia::media::ActivityReporter, std::unique_ptr<ActivityReporterImpl>>
      bindings_;
};
}  // namespace media::audio
#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ACTIVITY_DISPATCHER_H_
