// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTION_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTION_DISPATCHER_H_

#include <zircon/rights.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <arch/exception.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <object/dispatcher.h>
#include <object/thread_dispatcher.h>

// Zircon channel-based exception handling uses two primary classes,
// ExceptionDispatcher (this file) and Exceptionate (exceptionate.h).
//
// An ExceptionDispatcher represents a single currently-active exception. This
// will be transmitted to registered exception handlers in userspace and
// provides them with exception state and control functionality.
//
// An Exceptionate wraps a channel endpoint to help with sending exceptions to
// userspace. It is a kernel-internal helper class and not exposed to userspace.

class ExceptionDispatcher final
    : public SoloDispatcher<ExceptionDispatcher, ZX_DEFAULT_EXCEPTION_RIGHTS> {
 public:
  // Returns nullptr on memory allocation failure.
  static fbl::RefPtr<ExceptionDispatcher> Create(fbl::RefPtr<ThreadDispatcher> thread,
                                                 zx_excp_type_t exception_type,
                                                 const zx_exception_report_t* report,
                                                 const arch_exception_context_t* arch_context);

  static zx_exception_report_t BuildArchReport(uint32_t type,
                                               const arch_exception_context_t& arch_context);

  ~ExceptionDispatcher() final;

  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EXCEPTION; }

  // Marks the current exception handler as done.
  //
  // Once a handle has been created around this object, either
  // WaitForHandleClose() or DiscardHandleClose() must be called to reset
  // our state for the next handler.
  void on_zero_handles() final;

  fbl::RefPtr<ThreadDispatcher> thread() const { return thread_; }
  zx_excp_type_t exception_type() const { return exception_type_; }

  // Copies the exception report provided at ExceptionDispatcher creation.
  //
  // The exception report is only available while the exception thread is
  // still alive.
  //
  // Returns false and leaves |report| untouched if the thread has died.
  bool FillReport(zx_exception_report_t* report) const;

  // Sets the task rights to use for subsequent handle creation.
  //
  // rights == 0 indicates that the current exception handler is not allowed
  // to access the corresponding task handle, for example a thread-level
  // handler cannot access its parent process handle.
  //
  // This must only be called by an Exceptionate before transmitting the
  // exception - we don't ever want to be changing task rights while the
  // exception is out in userspace.
  void SetTaskRights(zx_rights_t thread_rights, zx_rights_t process_rights);

  // Creates new thread or process handles.
  //
  // Returns:
  //   ZX_OK on success.
  //   ZX_ERR_ACCESS_DENIED if the task rights have been set to 0.
  //   ZX_ERR_NO_MEMORY if the Handle failed to allocate.
  zx_status_t MakeThreadHandle(HandleOwner* handle) const;
  zx_status_t MakeProcessHandle(HandleOwner* handle) const;

  // Whether to resume the thread on exception close or pass it to the
  // next handler in line.
  void GetResumeThreadOnClose(bool* resume_on_close) const;
  void SetResumeThreadOnClose(bool resume_on_close);

  // Blocks until the exception handler is done processing.
  //
  // This must be called exactly once every time this exception is
  // successfully sent out to userspace, in order to wait for the response
  // and reset the internal state.
  //
  // Returns:
  //   ZX_OK if the exception was handled and the thread should resume.
  //   ZX_ERR_NEXT if the exception should be passed to the next handler.
  //   ZX_ERR_INTERNAL_INTR_KILLED if the thread was killed.
  zx_status_t WaitForHandleClose();

  // Resets the exception state for the next handler.
  //
  // This must be called instead of WaitForHandleClose() if a handle is
  // created around this exception but fails to make it out to userspace,
  // in order to reset the internal state.
  void DiscardHandleClose();

  // Wipe out exception state, which indicates the thread has died.
  void Clear();

 private:
  ExceptionDispatcher(fbl::RefPtr<ThreadDispatcher> thread, zx_excp_type_t exception_type,
                      const zx_exception_report_t* report,
                      const arch_exception_context_t* arch_context);

  // These are const and only set during construction, so don't need to be
  // guarded with get_lock().
  const fbl::RefPtr<ThreadDispatcher> thread_;
  const zx_excp_type_t exception_type_;

  // These gets updated by the Exceptionate whenever we get transmitted,
  // according to the rights that specific Exceptionate was registered with.
  zx_rights_t thread_rights_ TA_GUARDED(get_lock()) = 0;
  zx_rights_t process_rights_ TA_GUARDED(get_lock()) = 0;

  // These will be nulled out if the underlying thread is killed while
  // userspace still has access to this exception.
  const zx_exception_report_t* report_ TA_GUARDED(get_lock());
  const arch_exception_context_t* arch_context_ TA_GUARDED(get_lock());

  bool resume_on_close_ TA_GUARDED(get_lock()) = false;
  Event response_event_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTION_DISPATCHER_H_
