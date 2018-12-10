// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_ENGINE_H_
#define GARNET_LIB_UI_GFX_ENGINE_ENGINE_H_

#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>
#include <set>
#include <vector>

#include "lib/escher/escher.h"
#include "lib/escher/flib/release_fence_signaller.h"
#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/vk/simple_image_factory.h"

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/engine/resource_linker.h"
#include "garnet/lib/ui/gfx/engine/session_manager.h"
#include "garnet/lib/ui/gfx/engine/update_scheduler.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/util/event_timestamper.h"
#include "garnet/lib/ui/scenic/event_reporter.h"

namespace scenic_impl {
namespace gfx {

class Compositor;
class EngineRenderer;
class Session;
class SessionHandler;
class View;
class ViewHolder;
class Swapchain;

using ViewLinker = ObjectLinker<ViewHolder, View>;

// Graphical context for a set of session updates.
class CommandContext {
 public:
  CommandContext(escher::BatchGpuUploaderPtr uploader);

  escher::BatchGpuUploader* batch_gpu_uploader() const {
    FXL_DCHECK(batch_gpu_uploader_.get());
    return batch_gpu_uploader_.get();
  }

  // Flush any work accumulated during command processing.
  void Flush();

 private:
  escher::BatchGpuUploaderPtr batch_gpu_uploader_;
};

// Owns a group of sessions which can share resources with one another
// using the same resource linker and which coexist within the same timing
// domain using the same frame scheduler.  It is not possible for sessions
// which belong to different engines to communicate with one another.
class Engine : public UpdateScheduler, private FrameSchedulerDelegate {
 public:
  Engine(DisplayManager* display_manager, escher::EscherWeakPtr escher);

  ~Engine() override;

  DisplayManager* display_manager() const { return display_manager_; }
  escher::Escher* escher() const { return escher_.get(); }
  escher::EscherWeakPtr GetEscherWeakPtr() const { return escher_; }

  vk::Device vk_device() {
    return escher_ ? escher_->vulkan_context().device : vk::Device();
  }

  bool has_vulkan() const { return has_vulkan_; }

  escher::ResourceRecycler* escher_resource_recycler() {
    return escher_ ? escher_->resource_recycler() : nullptr;
  }

  escher::ImageFactory* escher_image_factory() { return image_factory_.get(); }

  escher::impl::GpuUploader* escher_gpu_uploader() {
    return escher_ ? escher_->gpu_uploader() : nullptr;
  }

  escher::RoundedRectFactory* escher_rounded_rect_factory() {
    return rounded_rect_factory_.get();
  }

  escher::ReleaseFenceSignaller* release_fence_signaller() {
    return release_fence_signaller_.get();
  }

  ResourceLinker* resource_linker() { return &resource_linker_; }
  ViewLinker* view_linker() { return &view_linker_; }

  EventTimestamper* event_timestamper() { return &event_timestamper_; }

  SessionManager* session_manager() { return session_manager_.get(); }

  FrameScheduler* frame_scheduler() { return frame_scheduler_.get(); }

  EngineRenderer* renderer() { return engine_renderer_.get(); }

  // |UpdateScheduler|
  //
  // Tell the FrameScheduler to schedule a frame. This is also used for updates
  // triggered by something other than a Session update i.e. an ImagePipe with
  // a new Image to present.
  void ScheduleUpdate(uint64_t presentation_time) override;

  // Create a swapchain for the specified display.  The display must not already
  // be claimed by another swapchain.
  std::unique_ptr<Swapchain> CreateDisplaySwapchain(Display* display);

  // Returns the first compositor in the current compositors, or nullptr if no
  // compositor exists.
  Compositor* GetFirstCompositor() const;

  // Returns the compositor requested, or nullptr if it does not exist.
  Compositor* GetCompositor(scenic_impl::GlobalId compositor_id) const;

  // Dumps the contents of all scene graphs.
  std::string DumpScenes() const;

  // Used by GpuMemory to import VMOs from clients.
  uint32_t imported_memory_type_index() const {
    return imported_memory_type_index_;
  }

  // The CommandContext is only valid during RenderFrame() and should not be
  // accessed outside of that.
  const CommandContext* GetCommandContext() {
    FXL_DCHECK(command_context_);
    return command_context_.get();
  }

  // Invoke Escher::Cleanup().  If more work remains afterward, post a delayed
  // task to try again; this is typically because cleanup couldn't finish due to
  // unfinished GPU work.
  void CleanupEscher();

 protected:
  // Only used by subclasses used in testing.
  Engine(DisplayManager* display_manager,
         std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller,
         std::unique_ptr<SessionManager> session_manager,
         escher::EscherWeakPtr escher);

 private:
  friend class Compositor;

  // Compositors register/unregister themselves upon creation/destruction.
  void AddCompositor(Compositor* compositor);
  void RemoveCompositor(Compositor* compositor);

  // |FrameSchedulerDelegate|:
  bool RenderFrame(const FrameTimingsPtr& frame, uint64_t presentation_time,
                   uint64_t presentation_interval, bool force_render) override;

  void InitializeFrameScheduler();
  void InitializeShaderFs();

  // Apply updates to all sessions who have updates and have acquired all
  // fences.  Return true if there were any updates applied.
  bool UpdateSessions(uint64_t presentation_time,
                      uint64_t presentation_interval,
                      uint64_t frame_number_for_tracing);

  // Update and deliver metrics for all nodes which subscribe to metrics events.
  void UpdateAndDeliverMetrics(uint64_t presentation_time);

  // Update reported metrics for nodes which subscribe to metrics events.
  // If anything changed, append the node to |updated_nodes|.
  void UpdateMetrics(Node* node,
                     const ::fuchsia::ui::gfx::Metrics& parent_metrics,
                     std::vector<Node*>* updated_nodes);

  // Set up and tear down a CommandContext for use in UpdateSessions().
  void InitializeCommandContext(uint64_t frame_number_for_tracing);
  void FlushAndInvalidateCommandContext();

  DisplayManager* const display_manager_;
  const escher::EscherWeakPtr escher_;

  std::unique_ptr<EngineRenderer> engine_renderer_;

  ResourceLinker resource_linker_;
  ViewLinker view_linker_;

  EventTimestamper event_timestamper_;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  std::unique_ptr<escher::RoundedRectFactory> rounded_rect_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
  std::unique_ptr<SessionManager> session_manager_;
  std::unique_ptr<FrameScheduler> frame_scheduler_;
  std::set<Compositor*> compositors_;

  std::unique_ptr<CommandContext> command_context_;

  bool escher_cleanup_scheduled_ = false;

  uint32_t imported_memory_type_index_ = 0;

  bool render_continuously_ = false;
  bool has_vulkan_ = false;

  fxl::WeakPtrFactory<Engine> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_ENGINE_H_
