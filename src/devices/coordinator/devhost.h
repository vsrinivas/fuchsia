// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_COORDINATOR_DEVHOST_H_
#define SRC_DEVICES_COORDINATOR_DEVHOST_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/process.h>

#include <utility>

#include <fbl/intrusive_double_list.h>

#include "device.h"

namespace devmgr {

class Device;

class Devhost {
 public:
  enum Flags : uint32_t {
    kDying = 1 << 0,
    kSuspend = 1 << 1,
  };

  struct AllDevhostsNode {
    static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(Devhost& obj) { return obj.anode_; }
  };
  struct SuspendNode {
    static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(Devhost& obj) { return obj.snode_; }
  };
  struct Node {
    static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(Devhost& obj) { return obj.node_; }
  };

  zx_handle_t hrpc() const { return hrpc_; }
  void set_hrpc(zx_handle_t hrpc) { hrpc_ = hrpc; }
  zx::unowned_process proc() const { return zx::unowned_process(proc_); }
  void set_proc(zx::process proc) { proc_ = std::move(proc); }
  zx_koid_t koid() const { return koid_; }
  void set_koid(zx_koid_t koid) { koid_ = koid; }
  // Note: this is a non-const reference to make |= etc. ergonomic.
  uint32_t& flags() { return flags_; }
  Devhost* parent() const { return parent_; }
  void set_parent(Devhost* parent) { parent_ = parent; }
  fbl::DoublyLinkedList<Device*, Device::DevhostNode>& devices() { return devices_; }
  fbl::DoublyLinkedList<Devhost*, Node>& children() { return children_; }

  // Returns a device id that will be unique within this devhost.
  uint64_t new_device_id() { return next_device_id_++; }

  // The AddRef and Release functions follow the contract for fbl::RefPtr.
  void AddRef() const { ++refcount_; }

  // Returns true when the last reference has been released.
  bool Release() const {
    const int32_t rc = refcount_;
    --refcount_;
    return rc == 1;
  }

 private:
  zx_handle_t hrpc_ = ZX_HANDLE_INVALID;
  zx::process proc_;
  zx_koid_t koid_ = 0;
  mutable int32_t refcount_ = 0;
  uint32_t flags_ = 0;
  Devhost* parent_ = nullptr;

  // The next ID to be allocated to a device in this devhost.  Skip 0 to make
  // an uninitialized value more obvious.
  uint64_t next_device_id_ = 1;

  // list of all devices on this devhost
  fbl::DoublyLinkedList<Device*, Device::DevhostNode> devices_;

  // listnode for this devhost in the all devhosts list
  fbl::DoublyLinkedListNodeState<Devhost*> anode_;

  // listnode for this devhost in the order-to-suspend list
  fbl::DoublyLinkedListNodeState<Devhost*> snode_;

  // listnode for this devhost in its parent devhost's list-of-children
  fbl::DoublyLinkedListNodeState<Devhost*> node_;

  // list of all child devhosts of this devhost
  fbl::DoublyLinkedList<Devhost*, Node> children_;
};

}  // namespace devmgr

#endif  // SRC_DEVICES_COORDINATOR_DEVHOST_H_
