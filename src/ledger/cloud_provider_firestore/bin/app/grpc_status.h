// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_GRPC_STATUS_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_GRPC_STATUS_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <grpc++/grpc++.h>

#include "src/ledger/cloud_provider_firestore/bin/include/types.h"

namespace cloud_provider_firestore {

cloud_provider::Status ConvertGrpcStatus(grpc::StatusCode status);

// If |status| is not OK, logs an error and returns true. Otherwise, returns
// false.
bool LogGrpcRequestError(const grpc::Status& status);

// If |status| is not OK, logs an error and returns true. Otherwise, returns
// false.
bool LogGrpcConnectionError(const grpc::Status& status);

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_GRPC_STATUS_H_
