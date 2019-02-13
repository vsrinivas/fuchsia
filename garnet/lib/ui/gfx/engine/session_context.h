// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_CONTEXT_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_CONTEXT_H_

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "lib/escher/escher.h"
#include "lib/escher/flib/release_fence_signaller.h"
#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/shape/rounded_rect_factory.h"

namespace scenic_impl {
namespace gfx {

class SessionManager;
class FrameScheduler;
class UpdateScheduler;
class EventTimestamper;
class DisplayManager;
class SceneGraph;
class ResourceLinker;
class View;
class ViewHolder;
using ViewLinker = ObjectLinker<ViewHolder, View>;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// Contains dependencies needed by Session. Used to decouple Session from
// Engine; enables dependency injection in tests.
//
// The objects in SessionContext must be guaranteed to have a lifecycle
// longer than Session. For this reason, SessionContext should not be passed
// from Session to other classes.
struct SessionContext {
  vk::Device vk_device;
  escher::Escher* escher;
  uint32_t imported_memory_type_index;
  escher::ResourceRecycler* escher_resource_recycler;
  escher::ImageFactory* escher_image_factory;
  // TODO(SCN-1168): Remove |escher_rounded_rect_factory| from here.
  escher::RoundedRectFactory* escher_rounded_rect_factory;
  escher::ReleaseFenceSignaller* release_fence_signaller;
  EventTimestamper* event_timestamper;
  SessionManager* session_manager;
  FrameScheduler* frame_scheduler;
  DisplayManager* display_manager;
  SceneGraphWeakPtr scene_graph;
  ResourceLinker* resource_linker;
  ViewLinker* view_linker;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_CONTEXT_H_
