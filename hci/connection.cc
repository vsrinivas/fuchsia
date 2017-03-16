// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

namespace bluetooth {
namespace hci {

// static
ftl::RefPtr<Connection> Connection::NewLEConnection(ConnectionHandle handle, Role role,
                                                    const LEConnectionParams& params) {
  FTL_DCHECK(handle);

  // The connection interval should be populated for a valid connection.
  FTL_DCHECK(params.connection_interval());

  // We cannot use make_unique here because the default constructor is private.
  auto conn = ftl::AdoptRef(new Connection);
  conn->handle_ = handle;
  conn->type_ = LinkType::kLE;
  conn->role_ = role;
  conn->le_conn_params_ = std::make_unique<LEConnectionParams>(params);

  return conn;
}

const LEConnectionParams* Connection::GetLEConnectionParams() const {
  // |le_conn_params_ != nullptr <-> type_ == LinkType::kLE.
  // |le_conn_params_ == nullptr <-> type_ != LinkType::kLE.
  FTL_DCHECK(!!le_conn_params_.get() == (type_ == LinkType::kLE));
  return le_conn_params_.get();
}

}  // namespace hci
}  // namespace bluetooth
