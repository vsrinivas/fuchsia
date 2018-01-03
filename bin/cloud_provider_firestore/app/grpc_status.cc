// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/grpc_status.h"

namespace cloud_provider_firestore {

cloud_provider::Status ConvertGrpcStatus(grpc::StatusCode status) {
  switch (status) {
    case grpc::OK:
      return cloud_provider::Status::OK;
    case grpc::UNAUTHENTICATED:
      return cloud_provider::Status::AUTH_ERROR;
    case grpc::NOT_FOUND:
      return cloud_provider::Status::NOT_FOUND;
    case grpc::UNAVAILABLE:
      return cloud_provider::Status::NETWORK_ERROR;
    default:
      return cloud_provider::Status::SERVER_ERROR;
  }
}

}  // namespace cloud_provider_firestore
