// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_ENTITY_ENTITY_WATCHER_IMPL_H_
#define PERIDOT_LIB_ENTITY_ENTITY_WATCHER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

#include <memory>

namespace modular {

// An EntityWatcher implementation which calls a callback when the entity data
// is updated.
class EntityWatcherImpl : public fuchsia::modular::EntityWatcher {
 public:
  EntityWatcherImpl();
  EntityWatcherImpl(
      fit::function<void(std::unique_ptr<fuchsia::mem::Buffer> value)>
          callback);

  // Sets the callback which will be called with the updated entity value.
  void SetOnUpdated(
      fit::function<void(std::unique_ptr<fuchsia::mem::Buffer> value)>
          callback);

  // Binds |request| to this EntityWatcher implementation.
  void Connect(fidl::InterfaceRequest<fuchsia::modular::EntityWatcher> request);

 private:
  // |EntityWatcher|
  void OnUpdated(std::unique_ptr<fuchsia::mem::Buffer> value);

  // The callback which is called when the entity data update is observed.
  fit::function<void(std::unique_ptr<fuchsia::mem::Buffer> value)> callback_;

  // The binding which is used by |Connect()| to bind incoming EntityWatcher
  // requests.
  fidl::Binding<fuchsia::modular::EntityWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityWatcherImpl);
};

}  // namespace modular

#endif  // PERIDOT_LIB_ENTITY_ENTITY_WATCHER_IMPL_H_
