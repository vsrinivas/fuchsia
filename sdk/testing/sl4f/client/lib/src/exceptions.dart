// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Wraps the error field of a JSON RPC as an [Exception].
class JsonRpcException implements Exception {
  final String request;
  final dynamic error;

  JsonRpcException(this.request, this.error);

  @override
  String toString() {
    return 'JSON RPC returned error: $error\nRequest: $request';
  }
}

/// Any exception when dealing with the SL4F server itself. For example failing
/// to start or terminate the server.
class Sl4fException implements Exception {
  String error;
  Sl4fException(this.error);

  @override
  String toString() {
    return 'Error when handling SL4F: $error';
  }
}
