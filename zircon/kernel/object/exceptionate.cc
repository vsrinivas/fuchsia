// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/exceptionate.h"

#include <zircon/errors.h>
#include <zircon/syscalls/exception.h>

#include <object/exception_dispatcher.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

Exceptionate::Exceptionate(uint32_t type) : type_(type) {}

Exceptionate::~Exceptionate() { Shutdown(); }

zx_status_t Exceptionate::SetChannel(KernelHandle<ChannelDispatcher> channel_handle,
                                     zx_rights_t thread_rights, zx_rights_t process_rights) {
  if (!channel_handle.dispatcher()) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<fbl::Mutex> guard{&lock_};

  if (is_shutdown_) {
    return ZX_ERR_BAD_STATE;
  }

  if (HasValidChannelLocked()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  // At this point we're certain that either there is no channel or it's a
  // dead channel with no peer (since channel endpoints can't re-open) so we
  // can overwrite it.
  channel_handle_ = ktl::move(channel_handle);
  thread_rights_ = thread_rights;
  process_rights_ = process_rights;

  return ZX_OK;
}

void Exceptionate::Shutdown() {
  Guard<fbl::Mutex> guard{&lock_};
  channel_handle_.reset();
  is_shutdown_ = true;
}

bool Exceptionate::HasValidChannel() const {
  Guard<fbl::Mutex> guard{&lock_};
  return HasValidChannelLocked();
}

bool Exceptionate::HasValidChannelLocked() const {
  return channel_handle_.dispatcher() && !channel_handle_.dispatcher()->PeerHasClosed();
}

zx_status_t Exceptionate::SendException(const fbl::RefPtr<ExceptionDispatcher>& exception) {
  DEBUG_ASSERT(exception);

  Guard<fbl::Mutex> guard{&lock_};

  if (!channel_handle_.dispatcher()) {
    return ZX_ERR_NEXT;
  }

  zx_exception_info_t info{};

  // Since info will be copied to a usermode process make sure it's safe to to be copied (no
  // internal padding, trivially copyable, etc.).
  static_assert(internal::is_copy_allowed<decltype(info)>::value);

  info.tid = exception->thread()->get_koid();
  info.pid = exception->thread()->process()->get_koid();
  info.type = exception->exception_type();

  MessagePacketPtr message;
  zx_status_t status =
      MessagePacket::Create(reinterpret_cast<const char*>(&info), sizeof(info), 1, &message);
  if (status != ZX_OK) {
    return status;
  }

  // It's OK if the function fails after this point, all exception sending
  // funnels through here so the task rights will get overwritten next time
  // we try to send it.
  //
  // This is safe to do because we know that an ExceptionDispatcher only goes
  // to one handler at a time, so we'll never change the task rights while
  // the exception is out in userspace.
  exception->SetTaskRights(thread_rights_, process_rights_);

  HandleOwner exception_handle(Handle::Make(exception, ExceptionDispatcher::default_rights()));
  if (!exception_handle) {
    return ZX_ERR_NO_MEMORY;
  }
  message->mutable_handles()[0] = exception_handle.release();
  message->set_owns_handles(true);

  status = channel_handle_.dispatcher()->Write(ZX_KOID_INVALID, ktl::move(message));

  // If sending failed for any reason, the exception handle never made it to
  // userspace and has now gone out of scope, triggering on_zero_handles(),
  // so we need to reset the exception.
  if (status != ZX_OK) {
    exception->DiscardHandleClose();
  }

  // ZX_ERR_PEER_CLOSED just indicates that there's no longer an endpoint
  // to receive exceptions, simplify things for callers by collapsing this
  // into the ZX_ERR_NEXT case since it means the same thing.
  if (status == ZX_ERR_PEER_CLOSED) {
    channel_handle_.reset();
    return ZX_ERR_NEXT;
  }

  return status;
}
