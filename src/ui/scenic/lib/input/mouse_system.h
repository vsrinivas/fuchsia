// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_MOUSE_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_INPUT_MOUSE_SYSTEM_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/hit_tester.h"
#include "src/ui/scenic/lib/input/mouse_source_base.h"
#include "src/ui/scenic/lib/input/mouse_source_with_global_mouse.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Tracks input APIs.
class MouseSystem : public fuchsia::ui::pointer::augment::GlobalMouse {
 public:
  explicit MouseSystem(sys::ComponentContext* context,
                       std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                       HitTester& hit_tester_, RequestFocusFunc request_focus);
  ~MouseSystem() = default;

  void RegisterMouseSource(
      fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source_request,
      zx_koid_t client_view_ref_koid);

  // |fuchsia::ui::pointer::augment::GlobalMouse|
  void Upgrade(fidl::InterfaceHandle<fuchsia::ui::pointer::MouseSource> original,
               UpgradeCallback callback) override;

  // Injects a mouse event directly to the View with koid |event.target|.
  void InjectMouseEventExclusive(const InternalMouseEvent& event, StreamId stream_id);
  // Injects a mouse event by hit testing for appropriate targets.
  void InjectMouseEventHitTested(const InternalMouseEvent& event, StreamId stream_id);
  // Sends a "View exit" event to the current receiver of |stream_id|, if there is one, and resets
  // the tracking state for the mouse stream.
  void CancelMouseStream(StreamId stream_id);

 private:
  // Finds the ViewRef koid registered with the other side of the |original| channel and returns it.
  // Returns ZX_KOID_INVALID if the related channel isn't found.
  zx_koid_t FindViewRefKoidOfRelatedChannel(
      const fidl::InterfaceHandle<fuchsia::ui::pointer::MouseSource>& original) const;

  // Locates and sends an event to the MouseSource identified by |receiver|, if one exists.
  void SendEventToMouse(zx_koid_t receiver, const InternalMouseEvent& event, StreamId stream_id,
                        bool view_exit);

  // Updates all MouseSourceWithGlobalMouse, causing them to send any pending events.
  void UpdateGlobalMouse(const InternalMouseEvent& event);

  /// Construction-time state.
  // Reference to the ViewTreeSnapshot held by InputSystem.
  std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot_;
  HitTester& hit_tester_;
  const RequestFocusFunc request_focus_;

  //// Mouse state
  // Struct for tracking the mouse state of a particular event stream.
  struct MouseReceiver {
    zx_koid_t view_koid = ZX_KOID_INVALID;
    bool latched = false;
  };
  // Currently hovered/latched view for each mouse stream.
  std::unordered_map<StreamId, MouseReceiver> current_mouse_receivers_;
  // Currently targeted mouse receiver for exclusive streams.
  std::unordered_map<StreamId, zx_koid_t> current_exclusive_mouse_receivers_;
  // All MouseSource instances. Each instance can be the receiver of any number of mouse streams,
  // and each stream is captured in either |current_mouse_receivers_| or
  // |current_exclusive_mouse_receivers_|.
  std::unordered_map<zx_koid_t, std::unique_ptr<MouseSourceBase>> mouse_sources_;
  // Map of pointers to all MouseSourceWithGlobalMouse instances.
  // Must be cleaned up when the owning pointer in |mouse_sources_| is cleaned up.
  std::unordered_map<zx_koid_t, MouseSourceWithGlobalMouse*> global_mouse_sources_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_MOUSE_SYSTEM_H_
