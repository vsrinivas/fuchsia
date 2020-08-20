// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Any exception when dealing with the SL4F server or with its facades.
///
/// For example failing to start or terminate the server.
class Sl4fException implements Exception {
  dynamic error;
  Sl4fException(this.error);

  @override
  String toString() => 'Error when handling SL4F: $error';
}

/// Wraps the error field of a JSON RPC as an [Exception].
class JsonRpcException extends Sl4fException {
  final String request;

  JsonRpcException(this.request, dynamic error) : super(error);

  @override
  String toString() => 'JSON RPC returned error: $error\nRequest: $request';
}

/// An exception when forwarding ports from or to the DUT.
class PortForwardException extends Sl4fException {
  final String from;
  final String to;
  PortForwardException(this.from, this.to, dynamic error) : super(error);

  @override
  String toString() => 'Error forwarding $from -> $to: $error';
}
