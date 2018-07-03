// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_CLIENT_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_CLIENT_H_

#include <functional>
#include <memory>

#include <google/firestore/v1beta1/firestore.grpc.pb.h>
#include <grpc++/grpc++.h>
#include <lib/fxl/functional/closure.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

namespace cloud_provider_firestore {

// Client interface for the Listen call.
//
// No methods will be called after the associated ListenCallHandler is deleted.
class ListenCallClient {
 public:
  ListenCallClient() {}
  virtual ~ListenCallClient() {}

  // Called when the connection is established.
  //
  // It is only after receiving this call that any methods on the associated
  // ListenCallHandler can be called.
  virtual void OnConnected() = 0;

  // Called when a response is received.
  //
  // Can be called multiple times.
  virtual void OnResponse(
      google::firestore::v1beta1::ListenResponse response) = 0;

  // Called when the stream is closed.
  //
  // Might be called at any time before or after the OnConnected() call, when
  // an error shuts down the stream.
  //
  // This method will be called exactly once. No other methods will be called
  // after this one. No methods on the associated ListenCallHandler can be
  // called after this call is received.
  virtual void OnFinished(grpc::Status status) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallClient);
};

// Handler for the listen call.
//
// The client can delete this at any point, causing the underlying RPC to
// correctly terminate if needed.
class ListenCallHandler {
 public:
  ListenCallHandler() {}

  virtual ~ListenCallHandler() {}

  // Writes the given |request| into the outgoing stream.
  //
  // Can be only called after the |OnConnected()| notification on the associated
  // ListenCallClient.
  virtual void Write(google::firestore::v1beta1::ListenRequest request) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallHandler);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_CLIENT_H_
