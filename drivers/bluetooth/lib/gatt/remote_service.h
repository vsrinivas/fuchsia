// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/gatt/client.h"
#include "garnet/drivers/bluetooth/lib/gatt/remote_characteristic.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gatt {

// Callback type invoked to notify when GATT services get discovered.
class RemoteService;
using RemoteServiceWatcher = std::function<void(fbl::RefPtr<RemoteService>)>;

using ServiceList = std::vector<fbl::RefPtr<RemoteService>>;
using ServiceListCallback = fbl::Function<void(att::Status, ServiceList)>;

using RemoteServiceCallback = fbl::Function<void(fbl::RefPtr<RemoteService>)>;
using RemoteCharacteristicList = std::vector<RemoteCharacteristic>;

namespace internal {
class RemoteServiceManager;
}  // namespace internal

// Represents the state of a GATT service that was discovered on a remote
// device. Clients can interact with a remote GATT service by obtaining a
// RemoteService object from the GATT system.
//
// THREAD SAFETY:
//
// A RemoteService can be accessed from multiple threads. All continuations
// provided in |callback| parameters below will run on the GATT thread unless an
// async dispatcher is explicitly provided.
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

  // Adds a handler which will be called when this service gets removed.
  // Returns false if the service was already shut down. |callback| will be
  // posted on |dispatcher|.
  bool AddRemovedHandler(fxl::Closure handler, async_t* dispatcher = nullptr);

  // Performs characteristic discovery and reports the result asynchronously in
  // |callback|. Returns the cached results if characteristics were already
  // discovered.
  using CharacteristicCallback =
      fbl::Function<void(att::Status, const RemoteCharacteristicList&)>;
  void DiscoverCharacteristics(CharacteristicCallback callback,
                               async_t* dispatcher = nullptr);

  // Returns true if all contents of this service have been discovered. This can
  // only be called on the GATT thread and is primarily intended for unit tests.
  // Clients should not rely on this and use DiscoverCharacteristics() to
  // guarantee discovery.
  bool IsDiscovered() const;

  // Sends a write request to the characteristic with the given identifier.
  // This operation fails if characteristics have not been discovered.
  //
  // TODO(armansito): Add a ByteBuffer version.
  void WriteCharacteristic(IdType id,
                           std::vector<uint8_t> value,
                           att::StatusCallback callback,
                           async_t* dispatcher = nullptr);

 private:
  friend class fbl::RefPtr<RemoteService>;
  friend class internal::RemoteServiceManager;

  template <typename T>
  struct PendingCallback {
    PendingCallback(T callback, async_t* dispatcher)
        : callback(std::move(callback)), dispatcher(dispatcher) {
      FXL_DCHECK(this->callback);
    }

    T callback;
    async_t* dispatcher;
  };

  using PendingClosure = PendingCallback<fxl::Closure>;
  using PendingCharacteristicCallback = PendingCallback<CharacteristicCallback>;

  // A RemoteService can only be constructed by a RemoteServiceManager.
  RemoteService(const ServiceData& service_data,
                fxl::WeakPtr<Client> client,
                async_t* gatt_dispatcher);
  ~RemoteService() = default;

  bool alive() const __TA_REQUIRES(mtx_) { return !shut_down_; }

  // Returns true if called on the GATT dispatcher's thread. False otherwise.
  // Intended for assertions only.
  bool IsOnGattThread() const;

  // Returns a pointer to the characteristic with |id|. Returns nullptr if not
  // found.
  common::HostError GetCharacteristic(IdType id,
                                      RemoteCharacteristic** out_char);

  // Runs |task| on the GATT dispatcher. |mtx_| must not be held when calling
  // this method. This guarantees that this object's will live for the duration
  // of |task|.
  void RunGattTask(fbl::Closure task) __TA_EXCLUDES(mtx_);

  // Used to complete a characteristic discovery request.
  void ReportCharacteristics(att::Status status,
                             CharacteristicCallback callback,
                             async_t* dispatcher) __TA_EXCLUDES(mtx_);

  ServiceData service_data_;

  // All unguarded members below MUST be accessed via |gatt_dispatcher_|.
  async_t* gatt_dispatcher_;

  // The GATT Client bearer for performing remote procedures.
  fxl::WeakPtr<Client> client_;

  // Queued discovery requests. Accessed only on the GATT dispatcher.
  using PendingDiscoveryList = std::vector<PendingCharacteristicCallback>;
  PendingDiscoveryList pending_discov_reqs_;

  // True if characteristic discovery has completed. This must be accessed only
  // through |gatt_dispatcher_|.
  bool characteristics_ready_;

  // The known characteristics of this service. If not |chrcs_ready_|, this may
  // contain a partial list of characteristics stored during the discovery
  // process.
  //
  // NOTE: This collection gets populated on |gatt_dispatcher_| and does not get
  // modified after discovery finishes. It is not publicly exposed until
  // discovery completes.
  RemoteCharacteristicList characteristics_;

  // Guards the members below.
  std::mutex mtx_;

  // Set to true by ShutDown() which makes this service defunct. This happens
  // when the remote device that this service was found on removes this service
  // or gets disconnected.
  //
  // This member will only get modified on the GATT thread while holding |mtx_|.
  // Holding |mtx_| is not necessary when read on the GATT thread but necessary
  // for all other threads.
  bool shut_down_;

  // Called by ShutDown().
  std::vector<PendingClosure> rm_handlers_ __TA_GUARDED(mtx_);

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteService);
};

}  // namespace gatt
}  // namespace btlib
