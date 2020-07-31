// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SIGNAL_OBSERVER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SIGNAL_OBSERVER_H_

#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>

class Handle;

// SignalObserver implementations may register to be called when
// a signal becomes active on a particular Dispatcher.
//
// Implementations must be thread compatible, but need not be thread safe.
class SignalObserver : public fbl::DoublyLinkedListable<SignalObserver*> {
 public:
  SignalObserver() = default;

  // Called when the set of active signals matches an expected set.
  //
  // At the time this is call, it is safe to delete this object: the
  // caller will not interact with it again.
  //
  // WARNING: This is called under Dispatcher's lock
  virtual void OnMatch(zx_signals_t signals) = 0;

  // Called when the registered handle (which refers to a handle to the
  // Dispatcher object) is being destroyed/"closed"/transferred. (The
  // object itself may also be destroyed shortly afterwards.)
  //
  // At the time this is call, it is safe to delete this object: the
  // caller will not interact with it again.
  //
  // WARNING: This is called under Dispatcher's lock
  virtual void OnCancel(zx_signals_t signals) = 0;

  // Determine if this observer matches the given port and key.
  //
  // If true, this object will be removed.
  //
  // WARNING: This is called under Dispatcher's lock.
  virtual bool MatchesKey(const void* port, uint64_t key) { return false; }

 protected:
  virtual ~SignalObserver() = default;

 private:
  // Dispatcher state, guarded by Dispatcher's lock.
  friend class Dispatcher;
  zx_signals_t triggering_signals_;
  const Handle* handle_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SIGNAL_OBSERVER_H_
