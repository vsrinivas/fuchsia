// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/input/view_stack.h"
#include "src/ui/scenic/lib/scenic/system.h"

namespace scenic_impl {
namespace input {

// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as focus.
//
// The general flow of events is:
// DispatchCommand --[decide what/where]--> EnqueueEvent
class InputSystem : public System, public fuchsia::ui::policy::accessibility::PointerEventRegistry {
 public:
  static constexpr TypeId kTypeId = kInput;
  static const char* kName;

  explicit InputSystem(SystemContext context, gfx::Engine* engine);
  virtual ~InputSystem() = default;

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext context) override;

  fuchsia::ui::input::ImeServicePtr& text_sync_service() { return text_sync_service_; }

  fuchsia::ui::input::accessibility::PointerEventListenerPtr&
  accessibility_pointer_event_listener() {
    return accessibility_pointer_event_listener_;
  }

  bool IsAccessibilityPointerEventForwardingEnabled() const {
    return accessibility_pointer_event_listener_ &&
           accessibility_pointer_event_listener_.is_bound();
  }

  std::unordered_map<SessionId, EventReporterWeakPtr>& hard_keyboard_requested() {
    return hard_keyboard_requested_;
  }

  // |fuchsia.ui.policy.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener,
                RegisterCallback callback) override;

 private:
  gfx::Engine* const engine_;

  // Send hard keyboard events to Text Sync for dispatch via IME; this is the
  // intended flow for clients to receive *mediated* keyboard events.
  // The connection to Text Sync is shared between all dispatchers.
  fuchsia::ui::input::ImeServicePtr text_sync_service_;

  // By default, clients don't get hard keyboard events directly from Scenic.
  // Clients may request these events via the SetHardKeyboardDeliveryCmd;
  // this set remembers which sessions have opted in.  We need this map because
  // each InputCommandDispatcher works independently.
  std::unordered_map<SessionId, EventReporterWeakPtr> hard_keyboard_requested_;

  fidl::BindingSet<fuchsia::ui::policy::accessibility::PointerEventRegistry>
      accessibility_pointer_event_registry_;
  // We honor the first accessibility listener to register. A call to Register()
  // above will fail if there is already a registered listener.
  fuchsia::ui::input::accessibility::PointerEventListenerPtr accessibility_pointer_event_listener_;
};

// Per-session treatment of input commands.
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(CommandDispatcherContext context, gfx::Engine* engine,
                         InputSystem* input_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  // A buffer to store pointer events.
  //
  // This buffer is used only when an accessibility listener is intercepting
  // pointer events. This buffer stores incoming pointer events per stream, and
  // sends them either to the views or the accessibility listener.
  //
  // It holds to pointer events until the accessibility listener decides to
  // consume / reject them.
  class PointerEventBuffer {
   public:
    // Represents a parallel dispatch of pointer events. Position 0 of this
    // vector holds the top-most view.
    using DeferredPerViewPointerEvents =
        std::vector<std::pair<ViewStack::Entry, fuchsia::ui::input::PointerEvent>>;
    // Possible states of a stream.
    enum PointerIdStreamStatus {
      WAITING_RESPONSE = 0,  // accessibility listener hasn't responded yet.
      CONSUMED = 1,
      REJECTED = 2,
    };
    // Represents a stream of pointer events, where a stream is a sequence of
    // ADD -> * -> REMOVE pointer event phases.
    struct PointerIdStream {
      // The pointer events of this stream. Please note that each element
      // of this vector is another vector itself. The reason is because one
      // pointer event may turn into multiple touch events when there are
      // several views receiving in parallel the same event.
      std::vector<DeferredPerViewPointerEvents> events;
      // Cache the focusability of the top-most View for this stream;
      // the ViewStack's focus_change only tracks the current stream. This field
      // is set when the stream is added (new ADD event coming).
      bool focus_change = true;
    };

    PointerEventBuffer(InputCommandDispatcher* dispatcher);
    ~PointerEventBuffer();

    // Adds a parallel dispatch event list |views_and_events| to the latest
    // stream associated with |pointer_id|. It also takes
    // |accessibility_pointer_event|, which is sent to the listener depending on
    // the current stream status.
    void AddEvents(uint32_t pointer_id, DeferredPerViewPointerEvents views_and_events,
                   fuchsia::ui::input::accessibility::PointerEvent accessibility_pointer_event);

    // Adds a new stream associated with |pointer_id|. |focus_change|
    // defines whether the top most view is focusable or not.
    void AddStream(uint32_t pointer_id, bool focus_change);

    // Updates the oldest stream associated with |pointer_id|, triggering an
    // appropriate action depending on |handled|.
    // If |handled| == CONSUMED, continues sending events to the listener.
    // If |handled| == REJECTED, dispatches buffered pointer events to views.
    void UpdateStream(uint32_t pointer_id,
                      fuchsia::ui::input::accessibility::EventHandling handled);

    // Sets the status and focusability of view of the active stream for a
    // pointer ID.
    void SetActiveStreamInfo(uint32_t pointer_id, PointerIdStreamStatus status, bool focus_change) {
      active_stream_info_[pointer_id] = {status, focus_change};
    }

   private:
    // Dispatches a parallel set of events to views.
    void DispatchEvents(DeferredPerViewPointerEvents views_and_events);

    // Helper function to dispatch a focus event when a deferred parallel
    // dispatch of pointer events corresponds to a DOWN event and the top-most
    // view is focusable.
    void MaybeDispatchFocusEvent(
        const InputCommandDispatcher::PointerEventBuffer::DeferredPerViewPointerEvents&
            views_and_events,
        bool focus_change);

    InputCommandDispatcher* const dispatcher_;
    // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
    // key = pointer ID, value = a list of pointer streams. Every new stream is
    // added to the end of the list, where a consume / reject response from the
    // listener always removes the first element.
    std::unordered_map<uint32_t, std::deque<PointerIdStream>> buffer_;
    // Key = pointer ID, value = the status and focusability of the current
    // active stream.
    //
    // This is kept separate from the map above because this must outlive
    // the stream itself. When the accessibility listener responds, the first
    // non-processed stream is consumed / rejected and gets removed from the
    // buffer. It may not be finished (we haven't seen a pointer event with
    // phase == REMOVE), so it is necessary to still keep track of where the
    // incoming pointer events should go, although they don't need to be
    // buffered anymore.
    // In addition, focusability of the top-most view for the stream is also
    // tracked here to deal with the case:
    // 1. Send ADD event. 2. a11y listener rejects the stream. 3. We remove the
    // buffered  stream, dispatching events. 4. An incoming down event must be
    // dispatched, but it needs the focusability information as well as the
    // status of the stream (rejected).
    // Whenever a pointer ID is added, its default value is WAITING_RESPONSE.
    std::unordered_map</*pointer ID*/ uint32_t,
                       std::pair<PointerIdStreamStatus, /*focusable*/ bool>>
        active_stream_info_;
  };

  // Per-command dispatch logic.
  void DispatchCommand(const fuchsia::ui::input::SendPointerInputCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SendKeyboardInputCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetHardKeyboardDeliveryCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetParallelDispatchCmd& command);

  // Per-pointer-type dispatch logic.
  void DispatchTouchCommand(const fuchsia::ui::input::SendPointerInputCmd& command);
  void DispatchMouseCommand(const fuchsia::ui::input::SendPointerInputCmd& command);

  // Enqueue the focus event into an EventReporter.
  static void ReportFocusEvent(EventReporter* reporter, fuchsia::ui::input::FocusEvent focus);

  // Enqueue the pointer event into the entry in a ViewStack.
  static void ReportPointerEvent(const ViewStack::Entry& view_info,
                                 fuchsia::ui::input::PointerEvent pointer);

  // Enqueue the keyboard event into an EventReporter.
  static void ReportKeyboardEvent(EventReporter* reporter,
                                  fuchsia::ui::input::KeyboardEvent keyboard);

  // Enqueue the keyboard event to the Text Sync service.
  static void SyncText(const fuchsia::ui::input::ImeServicePtr& text_sync,
                       fuchsia::ui::input::KeyboardEvent keyboard);

  // Maybe fires a focus event to a view.
  //
  //  The new focus can be either the old focus (either
  // deliberately, or by the no-focus property), or another view.
  // |focus_change| defines whether the top most view is focusable or not.
  // |view_info| is the top most view of a hit stack.
  void MaybeChangeFocus(bool focus_change, const ViewStack::Entry& view_info);

  // Checks if an accessibility listener is intercepting pointer events. If the
  // listener is on, initializes the buffer if it hasn't been created.
  // Important:
  // When the buffer is initialized, it can be the case that there are active
  // pointer event streams that haven't finished yet. They are sent to clients,
  // and *not* to the a11y listener. When the stream is done and a new stream
  // arrives, these will be sent to the a11y listener who will just continue its
  // normal flow. In a disconnection, if there are active pointer event streams,
  // its assume that the listener rejected them so they are sent to clients.
  bool ShouldForwardAccessibilityPointerEvents();

  // FIELDS

  gfx::Engine* const engine_ = nullptr;
  InputSystem* const input_system_ = nullptr;

  // Tracks which View has focus.
  ViewStack::Entry focus_;

  // Tracks the set of Views each touch event is delivered to; a map from
  // pointer ID to a stack of GlobalIds. This is used to ensure consistent
  // delivery of pointer events for a given finger to its original destination
  // targets on their respective DOWN event. In particular, a focus change
  // triggered by a new finger should *not* affect delivery of events to
  // existing fingers.
  //
  // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
  std::unordered_map<uint32_t, ViewStack> touch_targets_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to
  // a GlobalId. This is used to ensure consistent delivery of mouse events for
  // a given device. A focus change triggered by other pointer events should
  // *not* affect delivery of events to existing mice.
  //
  // NOTE: We reuse the ViewStack here just for convenience.
  std::unordered_map<uint32_t, ViewStack> mouse_targets_;

  // TODO(SCN-1047): Remove when gesture disambiguation is the default.
  bool parallel_dispatch_ = true;

  // When accessibility pointer event forwarding is enabled, this buffer stores
  // pointer events until an accessibility listener decides how to handle them.
  // It is always null otherwise.
  std::unique_ptr<PointerEventBuffer> pointer_event_buffer_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
