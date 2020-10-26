// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace flatland {

class FlatlandManager : public scheduling::SessionUpdater {
 public:
  FlatlandManager(
      const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
      const std::shared_ptr<UberStructSystem>& uber_struct_system,
      const std::shared_ptr<LinkSystem>& link_system,
      const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers);

  void CreateFlatland(fidl::InterfaceRequest<fuchsia::ui::scenic::internal::Flatland> flatland);

  // |scheduling::SessionUpdater|
  scheduling::SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;

  // |scheduling::SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<scheduling::SessionId,
                               std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
          latched_times,
      scheduling::PresentTimestamps present_times) override;

  // For validating test logic.
  size_t GetSessionCount() const;

 private:
  // Removes the Flatland instance associated with |session_id|.
  void RemoveFlatlandInstance(scheduling::SessionId session_id);

  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<LinkSystem> link_system_;
  std::vector<std::shared_ptr<BufferCollectionImporter>> buffer_collection_importers_;

  // Represents an individual Flatland session for a client.
  struct FlatlandInstance {
    // The looper for this Flatland instance, which will be run on a worker thread spawned by the
    // async::Loop itself. It must be the first member of this struct so that |binding| is
    // destroyed first in the default destruction order, else it will attempt to run on a shutdown
    // looper.
    async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

    // The channel and the implementation bound to it. This must come before |peer_closed_waiter|
    // so that the Wait is destroyed, and therefore cancelled, before the binding is destroyed in
    // the default destruction order.
    using ImplBinding =
        fidl::Binding<fuchsia::ui::scenic::internal::Flatland, std::unique_ptr<Flatland>>;
    std::shared_ptr<ImplBinding> binding;

    // Waits for the invalidation of the binding on the main thread cleans up this instance. Uses
    // WaitOnce since the handler will delete this FlatlandInstance.
    async::WaitOnce peer_closed_waiter;

    FlatlandInstance(const zx::channel& channel, std::unique_ptr<Flatland> flatland)
        : binding(std::make_shared<ImplBinding>(std::move(flatland))),
          peer_closed_waiter(channel.get(), ZX_CHANNEL_PEER_CLOSED) {}
  };

  // FlatlandInstances must be dynamically allocated because fidl::Binding is not movable.
  std::unordered_map<scheduling::SessionId, std::unique_ptr<FlatlandInstance>> flatland_instances_;

  // Sends |num_present_tokens| to a particular Flatland |instance|.
  void SendPresentTokens(FlatlandInstance* instance, uint32_t num_present_tokens);
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_
