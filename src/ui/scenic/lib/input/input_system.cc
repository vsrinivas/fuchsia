// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

namespace scenic_impl::input {

InputSystem::InputSystem(sys::ComponentContext* context, inspect::Node& inspect_node,
                         fxl::WeakPtr<gfx::SceneGraph> scene_graph, RequestFocusFunc request_focus)
    : request_focus_(std::move(request_focus)),
      hit_tester_(view_tree_snapshot_, inspect_node),
      mouse_system_(context, view_tree_snapshot_, hit_tester_,
                    [this](zx_koid_t koid) { request_focus_(koid); }),
      touch_system_(context, view_tree_snapshot_, hit_tester_, inspect_node,
                    std::move(scene_graph)),
      pointerinjector_registry_(
          context,
          /*inject_touch_exclusive=*/
          [&touch_system = touch_system_](const InternalTouchEvent& event, StreamId stream_id) {
            touch_system.InjectTouchEventExclusive(event, stream_id);
          },
          /*inject_touch_hit_tested=*/
          [&touch_system = touch_system_](const InternalTouchEvent& event, StreamId stream_id) {
            touch_system.InjectTouchEventHitTested(event, stream_id);
          },
          /*inject_mouse_exclusive=*/
          [&mouse_system = mouse_system_](const InternalMouseEvent& event, StreamId stream_id) {
            mouse_system.InjectMouseEventExclusive(event, stream_id);
          },
          /*inject_mouse_hit_tested=*/
          [&mouse_system = mouse_system_](const InternalMouseEvent& event, StreamId stream_id) {
            mouse_system.InjectMouseEventHitTested(event, stream_id);
          },
          // Explicit call necessary to cancel mouse stream, because mouse stream itself does not
          // track phase.
          /*cancel_mouse_stream=*/
          [&mouse_system = mouse_system_](StreamId stream_id) {
            mouse_system.CancelMouseStream(stream_id);
          },
          inspect_node.CreateChild("PointerinjectorRegistry")) {}

}  // namespace scenic_impl::input
