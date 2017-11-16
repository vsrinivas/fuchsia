// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {

namespace att {
class Attribute;
class Database;
class PacketReader;
}  // namespace att

namespace gatt {

// A GATT Server implements the server-role of the ATT protocol over a single
// ATT Bearer. A unique Server instance should exist for each logical link that
// supports GATT.
//
// A Server responds to incoming requests by querying the database that it
// is initialized with. Each Server shares an att::Bearer with a Client.
class Server final {
 public:
  // |database| will be queried by the Server to resolve transactions.
  // |bearer| is the ATT data bearer that this Server operates on.
  Server(fxl::RefPtr<att::Database> database, fxl::RefPtr<att::Bearer> bearer);
  ~Server();

 private:
  // ATT protocol request handlers:
  void OnExchangeMTU(att::Bearer::TransactionId tid,
                     const att::PacketReader& packet);
  void OnReadByGroupType(att::Bearer::TransactionId tid,
                         const att::PacketReader& packet);
  void OnReadByType(att::Bearer::TransactionId tid,
                    const att::PacketReader& packet);

  // Helper function to serve the Read By Type and Read By Group Type requests.
  // This searches |db| for attributes with the given |type| and adds as many
  // attributes as it can fit within the given |max_data_list_size|. The
  // attribute value that should be included within each attribute data list
  // entry is returned in |out_value_size|.
  //
  // If the result is a dynamic attribute, |out_results| will contain at most
  // one entry. |out_value_size| will point to an undefined value in that case.
  //
  // Returns att::ErrorCode::kNoError on success. On error, returns an error
  // code that can be used in a ATT Error Response.
  att::ErrorCode ReadByTypeHelper(
      att::Handle start,
      att::Handle end,
      const common::UUID& type,
      bool group_type,
      size_t max_data_list_size,
      size_t max_value_size,
      size_t entry_prefix_size,
      size_t* out_value_size,
      std::list<const att::Attribute*>* out_results);

  fxl::RefPtr<att::Database> db_;
  fxl::RefPtr<att::Bearer> att_;

  // ATT protocol request handler IDs
  att::Bearer::HandlerId exchange_mtu_id_;
  att::Bearer::HandlerId read_by_group_type_id_;
  att::Bearer::HandlerId read_by_type_id_;

  fxl::WeakPtrFactory<Server> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace gatt
}  // namespace btlib
