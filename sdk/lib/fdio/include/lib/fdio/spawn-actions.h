// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_SPAWN_ACTIONS_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_SPAWN_ACTIONS_H_

#include <lib/fdio/spawn.h>

#include <vector>

struct FdioSpawnActionWithHandle {
  fdio_spawn_action_t action;
  zx_handle_t handle;
};

// FdioSpawnActions maintains a fdio_spawn_action_t array and all the handles associated with the
// actions. All the handles would be closed on destruction unless 'GetActions' is called and then
// caller should pass the returned actions array that owns the handles to fdio_spawn_etc to transfer
// the handles.
class FdioSpawnActions {
 public:
  ~FdioSpawnActions() {
    for (FdioSpawnActionWithHandle action_with_handle : actions_with_handle_) {
      if (action_with_handle.handle != ZX_HANDLE_INVALID) {
        zx_handle_close(action_with_handle.handle);
      }
    }
  }

  void AddAction(fdio_spawn_action_t action) {
    zx::channel invalid_object;
    FdioSpawnActionWithHandle action_with_handle = {
        .action = action,
        .handle = ZX_HANDLE_INVALID,
    };
    actions_with_handle_.push_back(action_with_handle);
  }

  void AddActionWithHandle(fdio_spawn_action_t action, zx::object_base* object) {
    zx::channel invalid_object;
    action.h.handle = object->get();
    FdioSpawnActionWithHandle action_with_handle = {
        .action = action,
        .handle = object->release(),
    };
    actions_with_handle_.push_back(action_with_handle);
  }

  void AddActionWithNamespace(fdio_spawn_action_t action, zx::object_base* object) {
    zx::channel invalid_object;
    action.ns.handle = object->get();
    FdioSpawnActionWithHandle action_with_handle = {
        .action = action,
        .handle = object->release(),
    };
    actions_with_handle_.push_back(action_with_handle);
  }

  std::vector<fdio_spawn_action_t> GetActions() {
    // Return the stored actions array along with the ownership for all the associated objects back
    // to the caller.
    // Caller should call fdio_spawn_etc immediately after this call and this class's state would be
    // reinitialized.
    std::vector<fdio_spawn_action_t> actions;
    for (FdioSpawnActionWithHandle action_with_handle : actions_with_handle_) {
      actions.push_back(action_with_handle.action);
    }
    actions_with_handle_.clear();
    return actions;
  }

 private:
  std::vector<FdioSpawnActionWithHandle> actions_with_handle_;
};

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_SPAWN_ACTIONS_H_
