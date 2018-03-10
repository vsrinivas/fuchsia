// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/gatt/client.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {
namespace gatt {

// Callback type invoked to notify when GATT services get discovered.
class RemoteService;
using RemoteServiceWatcher = std::function<void(fbl::RefPtr<RemoteService>)>;

using ServiceList = std::vector<fbl::RefPtr<RemoteService>>;
using ServiceListCallback = std::function<void(att::Status, ServiceList)>;

namespace internal {
class RemoteServiceManager;
}  // namespace internal

// Represents the state of a GATT service that was discovered on a remote
// device. Clients can interact with a remote GATT service by obtaining a
// RemoteService object from the GATT system.
//
// THREAD SAFETY:
//
// All continuations provided in |callback| parameters below will run on the
// GATT thread unless an async dispatcher is explicitly provided.
class RemoteService : public fbl::RefCounted<RemoteService> {
 public:
  // Shuts down this service. Called when the service gets removed (e.g. due to
  // disconnection or because it was removed by the peer).
  void ShutDown();

  // Returns the service range start handle. This is used to uniquely identify
  // this service.
  att::Handle handle() const { return service_data_.range_start; }

  // Returns the service UUID.
  const common::UUID& uuid() const { return service_data_.type; }

  // Assigns a handler which will be called when this service gets removed.
  // Returns false if the service was already shut down.
  bool SetClosedCallback(fxl::Closure callback);

 private:
  friend class fbl::RefPtr<RemoteService>;
  friend class internal::RemoteServiceManager;

  // A RemoteService can only be constructed by a RemoteServiceManager.
  RemoteService(const ServiceData& service_data,
                fxl::WeakPtr<Client> client,
                async_t* gatt_dispatcher);
  ~RemoteService() = default;

  ServiceData service_data_;

  // The GATT Client bearer for performing remote procedures. |client_| can only
  // be accessed via |gatt_runner_|.
  fxl::WeakPtr<Client> client_;
  async_t* gatt_dispatcher_;

  // The members below represent shared state which is guarded by |mtx_|.
  std::mutex mtx_;

  // Set to true by ShutDown() which makes this service defunct. This happens
  // when the remote device that this service was found on  removes this service
  // or gets disconnected.
  bool shut_down_ __TA_GUARDED(mtx_);

  // Called by ShutDown().
  fxl::Closure closed_callback_ __TA_GUARDED(mtx_);

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteService);
};

}  // namespace gatt
}  // namespace btlib
