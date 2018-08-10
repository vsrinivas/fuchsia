// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_

#include <map>

#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/l2cap/scoped_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"
#include "garnet/drivers/bluetooth/lib/sdp/service_record.h"

#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace sdp {

// The SDP server object owns the Service Database and all Service Records.
// Only one server is expected to exist per host.
// This object is not thread-safe.
// TODO(jamuraa): make calls thread-safe or ensure single-threadedness
class Server final {
 public:
  // A new SDP server, which starts with the ServiceDiscoveryService record.
  Server();
  ~Server() = default;

  // Initialize a new SDP profile connection with |peer_id| on |channel|.
  // Returns false if the channel cannot be activated.
  bool AddConnection(const std::string& peer_id,
                     fbl::RefPtr<l2cap::Channel> channel);

  // Create a new ServiceRecord for a service, allocate a new handle for it, and
  // call |callback| synchronously populate it. When |callback| returns, the
  // record should have all attributes added.
  // The record will have a valid handle when |callback| is called.
  // The ServiceRecord passed to the callback is only guaranteed to be valid
  // while |callback| is run.
  // Returns |false| without calling |callback| if the record couldn't be
  // created.
  // Returns |true| if the record has been successfully added (if the record
  // remains valid).
  using ConstructCallback = fit::function<void(ServiceRecord*)>;
  bool RegisterService(ConstructCallback callback);

  // Unregister a service from the database. Idempotent.
  // Returns |true| if a record was removed.
  bool UnregisterService(ServiceHandle handle);

 private:
  // Inserts a new record in the database with handle |handle|, returning a
  // pointer to the newly constructed record.
  // Returns nullptr if the record already exists.
  ServiceRecord* MakeNewRecord(ServiceHandle handle);

  // Returns the next unused Service Handle, or 0 if none are available.
  ServiceHandle GetNextHandle();

  // Performs a Service Search, returning any service record that contains
  // all UUID from the |search_pattern|
  ServiceSearchResponse SearchServices(
      const std::unordered_set<common::UUID>& pattern) const;

  // Gets Service Attributes in the |attribute_ranges| from the service record
  // with |handle|.
  ServiceAttributeResponse GetServiceAttributes(
      ServiceHandle handle,
      const std::list<ServiceAttributeRequest::AttributeRange>& ranges) const;

  // l2cap::channel callbacks
  void OnChannelClosed(const std::string& peer_id);
  void OnRxBFrame(const std::string& peer_id, const l2cap::SDU& sdu);

  std::unordered_map<std::string, l2cap::ScopedChannel> channels_;
  std::unordered_map<ServiceHandle, ServiceRecord> records_;

  // The next available ServiceHandle.
  ServiceHandle next_handle_;

  // The service database state tracker.
  uint32_t db_state_ __UNUSED;

  fxl::WeakPtrFactory<Server> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace sdp
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_
