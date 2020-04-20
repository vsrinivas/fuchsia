// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVHOST_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVHOST_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/process.h>

#include <utility>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>

#include "device.h"

class Coordinator;
class Device;

class Devhost : public fbl::RefCounted<Devhost>, public fbl::DoublyLinkedListable<Devhost*> {
 public:
  // |coordinator| must outlive this Devhost object.
  explicit Devhost(Coordinator* coordinator);
  ~Devhost();

  enum Flags : uint32_t {
    kDying = 1 << 0,
    kSuspend = 1 << 1,
  };

  zx_handle_t hrpc() const { return hrpc_; }
  void set_hrpc(zx_handle_t hrpc) { hrpc_ = hrpc; }
  zx::unowned_process proc() const { return zx::unowned_process(proc_); }
  void set_proc(zx::process proc) { proc_ = std::move(proc); }
  zx_koid_t koid() const { return koid_; }
  void set_koid(zx_koid_t koid) { koid_ = koid; }
  // Note: this is a non-const reference to make |= etc. ergonomic.
  uint32_t& flags() { return flags_; }
  fbl::DoublyLinkedList<Device*, Device::DevhostNode>& devices() { return devices_; }

  // Returns a device id that will be unique within this devhost.
  uint64_t new_device_id() { return next_device_id_++; }

 private:
  Coordinator* coordinator_;

  zx_handle_t hrpc_ = ZX_HANDLE_INVALID;
  zx::process proc_;
  zx_koid_t koid_ = 0;
  uint32_t flags_ = 0;

  // The next ID to be allocated to a device in this devhost.  Skip 0 to make
  // an uninitialized value more obvious.
  uint64_t next_device_id_ = 1;

  // list of all devices on this devhost
  fbl::DoublyLinkedList<Device*, Device::DevhostNode> devices_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVHOST_H_
