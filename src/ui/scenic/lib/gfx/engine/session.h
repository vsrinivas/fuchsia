// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <unordered_map>
#include <vector>

#include "lib/inspect/cpp/inspect.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/scenic/lib/gfx/engine/buffer_collection.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/gfx/engine/resource_map.h"
#include "src/ui/scenic/lib/gfx/engine/session_context.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/resource_context.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/scenic/command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/present2_info.h"

namespace scenic_impl {
namespace gfx {

struct CommandContext;
class Resource;

// gfx::Session is the internal endpoint of the scenic::Session channel.
// It owns, and is responsible for, all graphics state on the channel
class Session : public CommandDispatcher {
 public:
  // Return type for ApplyScheduledUpdate
  struct ApplyUpdateResult {
    bool success;
    bool needs_render;
  };

  Session(SessionId id, SessionContext context,
          std::shared_ptr<EventReporter> event_reporter = EventReporter::Default(),
          std::shared_ptr<ErrorReporter> error_reporter = ErrorReporter::Default(),
          inspect::Node inspect_node = inspect::Node());
  virtual ~Session();

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override { debug_name_ = debug_name; }

  // |scenic::CommandDispatcher|
  // Virtual for testing.
  virtual void DispatchCommand(fuchsia::ui::scenic::Command command,
                               scheduling::PresentId) override;

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyCommand(CommandContext* command_context, fuchsia::ui::gfx::Command command) {
    return GfxCommandApplier::ApplyCommand(this, command_context, std::move(command));
  }

  scheduling::SessionId id() const { return id_; }

  const fxl::WeakPtr<Session> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }
  const SessionContext& session_context() const { return session_context_; }
  const ResourceContext& resource_context() const { return resource_context_; }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a
  // ResourceId. This number is decremented when a
  // ReleaseResourceCmd is applied.  However, the resource may continue to
  // exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  std::shared_ptr<ErrorReporter> shared_error_reporter() const { return error_reporter_; }
  ErrorReporter* error_reporter() const { return error_reporter_.get(); }
  EventReporter* event_reporter() const;  // Never nullptr.

  ResourceMap* resources() { return &resources_; }
  const ResourceMap* resources() const { return &resources_; }

  // Stage the latest ViewTree updates to a given SceneGraph.
  void UpdateAndStageViewTreeUpdates(SceneGraph* scene_graph);

  // Applies all updates with a PresentId up to and including |present_id| to the scene graph.
  // This function should be called with monotonically increasing PresentIds.
  // Returns true if the update succeeds, false otherwise.
  bool ApplyScheduledUpdates(CommandContext* command_context, scheduling::PresentId present_id);

  // Convenience.  Forwards an event to the EventReporter.
  void EnqueueEvent(::fuchsia::ui::gfx::Event event);
  void EnqueueEvent(::fuchsia::ui::input::InputEvent event);

  fxl::WeakPtr<ViewTreeUpdater> view_tree_updater() { return view_tree_updater_.GetWeakPtr(); }

  void RegisterBufferCollection(
      uint32_t buffer_collection_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token);

  void DeregisterBufferCollection(uint32_t buffer_collection_id);

  std::unordered_map<uint32_t, BufferCollectionInfo>& BufferCollections() {
    return buffer_collections_;
  }

 private:
  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  friend class GfxCommandApplier;
  bool SetRootView(fxl::WeakPtr<View> view);

  struct Update {
    scheduling::PresentId present_id = scheduling::kInvalidPresentId;
    std::vector<::fuchsia::ui::gfx::Command> commands;
    Update(scheduling::PresentId present_id, ::fuchsia::ui::gfx::Command command)
        : present_id(present_id) {
      commands.emplace_back(std::move(command));
    }
  };

  bool ApplyUpdate(CommandContext* command_context,
                   std::vector<::fuchsia::ui::gfx::Command> commands);

  std::queue<Update> scheduled_updates_;

  const scheduling::SessionId id_;
  std::string debug_name_;

  const std::shared_ptr<ErrorReporter> error_reporter_;
  const std::shared_ptr<EventReporter> event_reporter_;

  // Context objects should be above ResourceMap so they are destroyed after
  // Resources; their lifecycle must exceed that of the Resources.
  SessionContext session_context_;
  ResourceContext resource_context_;
  ResourceMap resources_;
  // The total number of live resources in this session, i.e. resources
  // which have been created and not yet destroyed. Note: there may be resources
  // alive that are not part of |resources_|, such as a Node that is referenced
  // by a parent Node in the scene graph.
  size_t resource_count_ = 0;
  // A weak reference to the first View added to the ResourceMap. Cached while
  // transitioning to a one-root-view-per-session model. See fxbug.dev/24450 for more
  // details.
  fxl::WeakPtr<View> root_view_;

  ViewTreeUpdater view_tree_updater_;

  scheduling::PresentId last_traced_present_id_ = 0;

  inspect::Node inspect_node_;
  inspect::UintProperty inspect_resource_count_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  std::unordered_map<uint32_t, BufferCollectionInfo> buffer_collections_;
  std::vector<BufferCollectionInfo> deregistered_buffer_collections_;

  fxl::WeakPtrFactory<Session> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_
