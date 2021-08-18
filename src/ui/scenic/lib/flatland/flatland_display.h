// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_DISPLAY_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_DISPLAY_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>

#include <functional>
#include <memory>

#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

namespace flatland {

// FlatlandDisplay implements the FIDL API of the same name.  It is the glue between a physical
// display and a tree of Flatland content attached underneath.
class FlatlandDisplay : public fuchsia::ui::composition::FlatlandDisplay,
                        public std::enable_shared_from_this<FlatlandDisplay> {
 public:
  using TransformId = fuchsia::ui::composition::TransformId;

  static std::shared_ptr<FlatlandDisplay> New(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> request,
      scheduling::SessionId session_id, std::shared_ptr<scenic_impl::display::Display> display,
      std::function<void()> destroy_display_function,
      std::shared_ptr<FlatlandPresenter> flatland_presenter,
      std::shared_ptr<LinkSystem> link_system,
      std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue);

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  FlatlandDisplay(const FlatlandDisplay&) = delete;
  FlatlandDisplay& operator=(const FlatlandDisplay&) = delete;
  FlatlandDisplay(FlatlandDisplay&&) = delete;
  FlatlandDisplay& operator=(FlatlandDisplay&&) = delete;

  // |fuchsia::ui::composition::FlatlandDisplay|
  void SetContent(fuchsia::ui::views::ViewportCreationToken token,
                  fidl::InterfaceRequest<fuchsia::ui::composition::ChildViewWatcher>
                      child_view_watcher) override;

  TransformHandle root_transform() const { return root_transform_; }
  scenic_impl::display::Display* display() const { return display_.get(); }

  scheduling::SessionId session_id() const { return session_id_; }

 private:
  FlatlandDisplay(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                  fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> request,
                  scheduling::SessionId session_id,
                  std::shared_ptr<scenic_impl::display::Display> display,
                  std::function<void()> destroy_display_function,
                  std::shared_ptr<FlatlandPresenter> flatland_presenter,
                  std::shared_ptr<LinkSystem> link_system,
                  std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue);

  // The dispatcher this Flatland display is running on.
  async_dispatcher_t* dispatcher() const { return dispatcher_holder_->dispatcher(); }
  std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;

  // The FIDL binding for this FlatlandDisplay, which references |this| as the implementation and
  // run on |dispatcher_|.
  fidl::Binding<fuchsia::ui::composition::FlatlandDisplay> binding_;

  // The unique SessionId for this FlatlandDisplay. Used to schedule Presents and register
  // UberStructs with the UberStructSystem.
  const scheduling::SessionId session_id_;

  // Physical display that this FlatlandDisplay connects to a tree of Flatland content.
  const std::shared_ptr<scenic_impl::display::Display> display_;

  // A function that, when called, will destroy this display. Necessary because an async::Wait can
  // only wait on peer channel destruction, not "this" channel destruction, so the FlatlandManager
  // cannot detect if this instance closes |binding_|.
  std::function<void()> destroy_display_function_;

  // Waits for the invalidation of the bound channel, then triggers the destruction of this client.
  // Uses WaitOnce since calling the handler will result in the destruction of this object.
  async::WaitOnce peer_closed_waiter_;

  // A FlatlandPresenter shared between Flatland sessions. Flatland uses this interface to get
  // PresentIds when publishing to the UberStructSystem.
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;

  // A link system shared between Flatland instances, so that links can be made between them.
  const std::shared_ptr<LinkSystem> link_system_;

  // An UberStructSystem shared between Flatland instances. Flatland publishes local data to the
  // UberStructSystem in order to have it seen by the global render loop.
  const std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue_;

  TransformGraph transform_graph_;

  const TransformHandle root_transform_;

  LinkSystem::ChildLink child_link_;

  // Must have a ViewRef as a reference for the UberStruct.
  std::shared_ptr<fuchsia::ui::views::ViewRef> view_ref_;
  std::unique_ptr<fuchsia::ui::views::ViewRefControl> control_ref_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_DISPLAY_H_
