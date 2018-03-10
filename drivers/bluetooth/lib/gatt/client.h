// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gatt {

// Types representing GATT procedure results.
struct ServiceData {
  ServiceData() = default;
  ServiceData(att::Handle start, att::Handle end, const common::UUID& type);

  att::Handle range_start;
  att::Handle range_end;
  common::UUID type;
};

// Implements GATT client-role procedures. A client operates over a single ATT
// data bearer. Client objects are solely used to map GATT procedures to ATT
// protocol methods and do not maintain service state.
//
// THREAD SAFETY:
//
// Client is not thread safe. It must be created, used, and destroyed on the
// same thread. All asynchronous callbacks are run on the thread that the data
// bearer is bound to.
class Client {
 public:
  using StatusCallback = std::function<void(att::Status status)>;

  // Constructs a new Client bearer.
  static std::unique_ptr<Client> Create(fxl::RefPtr<att::Bearer> bearer);

  virtual ~Client() = default;

  // Returns a weak pointer to this Client. The weak pointer should be checked
  // on the data bearer's thread only as Client can only be accessed on that
  // thread.
  virtual fxl::WeakPtr<Client> AsWeakPtr() = 0;

  // Initiates an MTU exchange and adjusts the MTU of the bearer according to
  // what the peer is capable of. The request will be initiated using the
  // bearer's preferred MTU.
  //
  // After the exchange is complete, the bearer will be updated to use the
  // resulting MTU. The resulting MTU will be notified via |callback|.
  //
  // |status| will be set to an error if the MTU exchange fails. The |mtu|
  // parameter will be set to 0 and the underlying bearer's MTU will remain
  // unmodified.
  using MTUCallback = std::function<void(att::Status status, uint16_t mtu)>;
  virtual void ExchangeMTU(MTUCallback callback) = 0;

  // Performs the "Discover All Primary Services" procedure defined in
  // v5.0, Vol 3, Part G, 4.4.1. |service_callback| is run for each discovered
  // service. |status_callback| is run with the result of the operation.
  //
  // NOTE: |service_callback| will be called asynchronously as services are
  // discovered so a caller can start processing the results immediately while
  // the procedure is in progress. Since discovery usually occurs over multiple
  // ATT transactions, it is possible for |status_callback| to be called with an
  // error even if some services have been discovered. It is up to the client
  // to clear any cached state in this case.
  using ServiceCallback = std::function<void(const ServiceData&)>;
  virtual void DiscoverPrimaryServices(ServiceCallback svc_callback,
                                       StatusCallback status_callback) = 0;
};

}  // namespace gatt
}  // namespace btlib
