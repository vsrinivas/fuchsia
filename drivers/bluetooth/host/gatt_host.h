// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/public/lib/bluetooth/fidl/gatt.fidl.h"

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bthost {

class Server;

// This object is responsible for bridging the GATT profile to the outside
// world.
//
// GattHost represents the GATT TaskDomain. It spawns and manages a thread on
// which all GATT tasks are serialized in an asynchronous event loop.
//
// This domain is responsible for:
//
//   * The GATT profile implementation over L2CAP;
//   * Creation of child GATT bt-gatt-svc devices and relaying of their
//     messages;
//   * FIDL message processing
//
// All functions on this object are thread safe. ShutDown() must be called
// at least once to properly clean up this object before destruction (this is
// asserted).
class GattHost final : public fbl::RefCounted<GattHost>,
                       public btlib::common::TaskDomain<GattHost> {
 public:
  static fbl::RefPtr<GattHost> Create(std::string thread_name);

  void Initialize();

  // This MUST be called to cleanly destroy this object.
  void ShutDown();

  // Closes all open FIDL interface handles.
  void CloseServers();

  // Binds the given request to a FIDL server.
  void BindGattServer(f1dl::InterfaceRequest<bluetooth::gatt::Server> request);

  // Returns the GATT profile implementation.
  fbl::RefPtr<btlib::gatt::GATT> profile() const { return gatt_; }

 private:
  BT_FRIEND_TASK_DOMAIN(GattHost);
  friend class fbl::RefPtr<GattHost>;

  explicit GattHost(std::string thread_name);
  ~GattHost() override = default;

  // Called by TaskDomain during shutdown. This must be called on the GATT task
  // runner.
  void CleanUp();

  // Adds a new FIDL server. Must be called on GATT task runner's thread.
  void AddServer(std::unique_ptr<Server> server);

  // The GATT profile.
  fbl::RefPtr<btlib::gatt::GATT> gatt_;

  // All currently active FIDL connections. These involve FIDL bindings that are
  // bound to |task_runner_|. These objects are highly thread hostile and must
  // be accessed only from |task_runner_|'s thread.
  std::unordered_map<Server*, std::unique_ptr<Server>> servers_;

  fxl::WeakPtrFactory<GattHost> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattHost);
};

}  // namespace bthost
