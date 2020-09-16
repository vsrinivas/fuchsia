// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_SESSION_H_
#define LIB_UI_SCENIC_CPP_SESSION_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>

#include <utility>

namespace scenic {

// Records the number of bytes occupied by enqueue requests without any commands.
//
// As commands are accumulated, they are measured the number of bytes and handles
// added to this base. See |Flush|, |commands_num_bytes_|, and
// |commands_num_handles_|.
constexpr int64_t kEnqueueRequestBaseNumBytes =
    sizeof(fidl_message_header_t) + sizeof(fidl_vector_t);

// Connect to Scenic and establish a new Session, as well as an InterfaceRequest
// for a SessionListener that can be hooked up as desired.
//
// Callbacks will be run on the async dispatcher specified by |dispatcher|,
// or the default dispatcher for the current thread if unspecified.
using SessionPtrAndListenerRequest =
    std::pair<fuchsia::ui::scenic::SessionPtr,
              fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>>;
SessionPtrAndListenerRequest CreateScenicSessionPtrAndListenerRequest(
    fuchsia::ui::scenic::Scenic* scenic, async_dispatcher_t* dispatcher = nullptr);

// Wraps a Scenic session.
// Maintains a queue of pending operations and assists with allocation of
// resource ids.
class Session : private fuchsia::ui::scenic::SessionListener {
 public:
  // Provides timing information about a presentation request which has
  // been applied by the scene manager.
  using PresentCallback = fit::function<void(fuchsia::images::PresentationInfo info)>;
  // Provides immediate information about predicted future latch and presentation times.
  using Present2Callback =
      fit::function<void(fuchsia::scenic::scheduling::FuturePresentationTimes info)>;
  // Provides immediate information about predicted future latch and presentation times.
  using RequestPresentationTimesCallback =
      fit::function<void(fuchsia::scenic::scheduling::FuturePresentationTimes info)>;

  // Called when session events are received.
  using EventHandler = fit::function<void(std::vector<fuchsia::ui::scenic::Event>)>;
  // Called when one or more Present2s are presented.
  using OnFramePresentedCallback =
      fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>;

  // Wraps the provided session and session listener.
  // The listener is optional.
  //
  // Callbacks for |session_listener| will be run on the async dispatcher
  // specified by |dispatcher|, or the default dispatcher for the current thread
  // if unspecified. Callbacks for |session| will be run on the dispatcher to
  // which it was bound before being passed in.
  explicit Session(
      fuchsia::ui::scenic::SessionPtr session,
      fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> session_listener = nullptr,
      async_dispatcher_t* dispatcher = nullptr);

  // Creates a new session using the provided Scenic and binds the listener to
  // this object. The Scenic itself is not retained after construction.
  //
  // Callbacks will be run on the async dispatcher specified by |dispatcher|, or
  // the default dispatcher for the current thread if unspecified.
  explicit Session(fuchsia::ui::scenic::Scenic* scenic, async_dispatcher_t* dispatcher = nullptr);
  explicit Session(fuchsia::ui::scenic::Scenic* scenic,
                   fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser,
                   async_dispatcher_t* dispatcher = nullptr);

  // Callbacks for SessionListener will be run on the async dispatcher specified
  // by |dispatcher|, or the default dispatcher for the current thread if
  // unspecified. Callbacks for SesssionPtr will be run on the dispatcher to
  // which it was bound before being passed in.
  explicit Session(SessionPtrAndListenerRequest session_and_listener,
                   async_dispatcher_t* dispatcher = nullptr);

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Destroys the session.
  // All resources must be released prior to destruction.
  ~Session();

  void set_error_handler(fit::function<void(zx_status_t)> closure) {
    session_.set_error_handler(std::move(closure));
  }

  // Sets a callback which is invoked when events are received.
  void set_event_handler(EventHandler event_handler) { event_handler_ = std::move(event_handler); }

  // Sets the callback invoked when frames are presented.
  void set_on_frame_presented_handler(OnFramePresentedCallback callback);

  // Gets a pointer to the underlying session interface.
  fuchsia::ui::scenic::Session* session() { return session_.get(); }

  // Gets the next resource id which will be provided when |AllocResourceId| is
  // called.
  uint32_t next_resource_id() const { return next_resource_id_; }

  // Allocates a new unique resource id.
  uint32_t AllocResourceId();

  // Enqueues an operation to release a resource.
  void ReleaseResource(uint32_t resource_id);

  // Enqueues an operation.
  // The session will queue operations locally to batch submission of operations
  // until |Flush()| or |Present()| is called.
  void Enqueue(fuchsia::ui::scenic::Command command);
  void Enqueue(fuchsia::ui::gfx::Command command);
  void Enqueue(fuchsia::ui::input::Command command);

  // Registers an acquire fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueAcquireFence(zx::event fence);

  // Registers a release fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueReleaseFence(zx::event fence);

  // Flushes queued operations to the session.
  // Virtual for testing.
  virtual void Flush();

  // Presents all previously enqueued operations.
  // Implicitly flushes all queued operations to the session.
  // Invokes the callback when the scene manager applies the presentation.
  void Present(uint64_t presentation_time, PresentCallback callback);

  // Overloaded |Present()| using zx::time.
  void Present(zx::time presentation_time, PresentCallback callback);

  // Immediately invokes the callback, providing predicted information back to the client.
  // Presents all previously enqueued operations.
  // Implicitly flushes all queued operations to the session.
  void Present2(zx_duration_t requested_presentation_time, zx_duration_t requested_prediction_span,
                Present2Callback immediate_callback);

  // Immediately invokes the callback, providing predicted information back to the client.
  void RequestPresentationTimes(zx_duration_t requested_prediction_span,
                                RequestPresentationTimesCallback callback);

  // Immediately registers the buffer collection.
  void RegisterBufferCollection(
      uint32_t buffer_collection_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token);

  // Immediately deregisters the buffer collection.
  void DeregisterBufferCollection(uint32_t buffer_collection_id);

  // Unbinds the internal SessionPtr; this allows moving this across threads.
  void Unbind();

  // Rebinds the Session interface internally; this must be called after a call
  // to Unbind().
  void Rebind();

  void SetDebugName(const std::string& debug_name);

 protected:
  std::vector<fuchsia::ui::scenic::Command> commands_;
  int64_t commands_num_bytes_ = kEnqueueRequestBaseNumBytes;
  int64_t commands_num_handles_ = 0;

 private:
  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override;
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override;

  fuchsia::ui::scenic::SessionPtr session_;
  // |session_handle_| is stored only when |session_| is unbound/invalid.
  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session_handle_;
  uint32_t next_resource_id_ = 1u;
  uint32_t resource_count_ = 0u;

  std::vector<zx::event> acquire_fences_;
  std::vector<zx::event> release_fences_;

  EventHandler event_handler_;
  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_SESSION_H_
