// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_CONTEXT_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_CONTEXT_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/shape/rounded_rect_factory.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {

class SessionManager;
class UpdateScheduler;
class Sysmem;
class DisplayManager;
class SceneGraph;
class ResourceLinker;
class View;
class ViewHolder;
using ViewLinker = ObjectLinker<ViewHolder*, View*>;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// Contains dependencies needed by Session. Used to decouple Session from
// Engine; enables dependency injection in tests.
//
// The objects in SessionContext must be guaranteed to have a lifecycle
// longer than Session. For this reason, SessionContext should not be passed
// from Session to other classes.
struct SessionContext {
  vk::Device vk_device;
  escher::Escher* escher = nullptr;
  escher::ResourceRecycler* escher_resource_recycler = nullptr;
  escher::ImageFactory* escher_image_factory = nullptr;
  // TODO(SCN-1168): Remove |escher_rounded_rect_factory| from here.
  escher::RoundedRectFactory* escher_rounded_rect_factory = nullptr;
  escher::ReleaseFenceSignaller* release_fence_signaller = nullptr;
  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler;
  SceneGraphWeakPtr scene_graph;
  ResourceLinker* resource_linker = nullptr;
  ViewLinker* view_linker = nullptr;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_CONTEXT_H_
