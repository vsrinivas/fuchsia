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
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace flatland {

class FlatlandManager {
 public:
  FlatlandManager(const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
                  const std::shared_ptr<Renderer>& renderer,
                  const std::shared_ptr<UberStructSystem>& uber_struct_system,
                  const std::shared_ptr<LinkSystem>& link_system);

  void CreateFlatland(fidl::InterfaceRequest<fuchsia::ui::scenic::internal::Flatland> flatland);

  // For validating test logic.
  size_t GetSessionCount() const;

 private:
  // Removes the Flatland instance associated with |session_id|.
  void RemoveFlatlandInstance(scheduling::SessionId session_id);

  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  std::shared_ptr<Renderer> renderer_;
  std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<LinkSystem> link_system_;

  // Represents an individual Flatland session for a client.
  struct FlatlandInstance {
    // The looper for this Flatland instance, which will be run on a worker thread spawned by the
    // async::Loop itself.
    async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

    // The channel and the implementation bound to it. This must come before |peer_closed_waiter|
    // so that the Wait is destroyed, and therefore cancelled, before the binding is destroyed in
    // the default destruction order.
    fidl::Binding<fuchsia::ui::scenic::internal::Flatland, std::unique_ptr<Flatland>> binding;

    // Waits for the invalidation of the binding on the main thread cleans up this instance. Uses
    // WaitOnce since the handler will delete this FlatlandInstance.
    async::WaitOnce peer_closed_waiter;

    FlatlandInstance(const zx::channel& channel, std::unique_ptr<Flatland> flatland)
        : binding(std::move(flatland)), peer_closed_waiter(channel.get(), ZX_CHANNEL_PEER_CLOSED) {}
  };
  std::unordered_map<scheduling::SessionId, std::unique_ptr<FlatlandInstance>> flatland_instances_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_
