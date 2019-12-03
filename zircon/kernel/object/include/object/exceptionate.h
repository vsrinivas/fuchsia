// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTIONATE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTIONATE_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>

class ExceptionDispatcher;

// Kernel-owned exception channel endpoint.
//
// This class is thread-safe, does not require external synchronization.
class Exceptionate {
 public:
  // Jobs and processes need to distinguish between standard or debug
  // exception handlers.
  enum class Type { kStandard, kDebug };

  // |type| must be a valid ZX_EXCEPTION_CHANNEL_TYPE_* constant.
  Exceptionate(uint32_t type);

  uint32_t type() const { return type_; }

  // Shuts the underlying channel down if it's still connected to be sure the
  // userspace endpoint gets the PEER_CLOSED signal.
  //
  // In most cases the task wants to manually shutdown the exceptionate when
  // transitioning to a dead state, but in some cases tasks can be destroyed
  // without registering the dead state e.g. childless jobs.
  ~Exceptionate();

  // Sets the backing ChannelDispatcher endpoint.
  //
  // The exception channel is first-come-first-served, so if there is
  // already a valid channel in place (i.e. has a live peer) this will
  // fail.
  //
  // The |*_rights| arguments give the rights to assign to task handles
  // provided through this exception channel. A value of 0 indicates that
  // the handle should not be made available through this channel.
  //
  // Returns:
  //   ZX_ERR_INVALID_ARGS if |channel| is null.
  //   ZX_ERR_ALREADY_BOUND is there is already a valid channel.
  //   ZX_ERR_BAD_STATE if Shutdown() has already been called.
  zx_status_t SetChannel(KernelHandle<ChannelDispatcher> channel_handle, zx_rights_t thread_rights,
                         zx_rights_t process_rights);

  // Removes any exception channel, which will signal PEER_CLOSED for the
  // userspace endpoint.
  //
  // Any further attempt to set a new channel will return ZX_ERR_BAD_STATE.
  void Shutdown();

  // Returns true if the channel exists and has a valid userspace peer.
  bool HasValidChannel() const;

  // Sends an exception to userspace.
  //
  // The exception message contains:
  //  * 1 struct: zx_exception_info_t
  //  * 1 handle: ExceptionDispatcher
  //
  // Returns:
  //   ZX_ERR_NEXT if there is no valid underlying channel.
  //   ZX_ERR_NO_MEMORY if we failed to allocate memory.
  zx_status_t SendException(const fbl::RefPtr<ExceptionDispatcher>& exception);

 private:
  bool HasValidChannelLocked() const TA_REQ(lock_);

  const uint32_t type_;
  mutable DECLARE_MUTEX(Exceptionate) lock_;
  KernelHandle<ChannelDispatcher> channel_handle_ TA_GUARDED(lock_);
  zx_rights_t thread_rights_ TA_GUARDED(lock_) = 0;
  zx_rights_t process_rights_ TA_GUARDED(lock_) = 0;
  bool is_shutdown_ TA_GUARDED(lock_) = false;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXCEPTIONATE_H_
