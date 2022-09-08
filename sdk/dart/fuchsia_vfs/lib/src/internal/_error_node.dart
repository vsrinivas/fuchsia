// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:zircon/zircon.dart';

// ignore_for_file: public_member_api_docs, unnecessary_null_comparison

typedef OnEventSent = Function(ErrorNodeForSendingEvent);

/// This node implementation is used to send on_open event if
/// there is a error opening connection to [Node].
class ErrorNodeForSendingEvent extends Node {
  final int _status;
  final OnEventSent _onEventSent;
  final NodeBinding _bindings = NodeBinding();

  /// Constructor
  ErrorNodeForSendingEvent(
      this._status, this._onEventSent, InterfaceRequest<Node> request)
      : assert(_status != ZX.OK),
        assert(request != null),
        assert(_onEventSent != null) {
    _bindings.bind(this, request);
  }

  @override
  Stream<Node$OnOpen$Response> get onOpen async* {
    yield Node$OnOpen$Response(_status, null);
    _onEventSent(this);
    _bindings.close();
  }

  // TODO(https://fxbug.dev/77623): Switch from onOpen to onRepresentation when
  // clients are ready and delete this class.
  @override
  Stream<Representation> get onRepresentation async* {}

  @override
  Future<void> clone(OpenFlags flags, InterfaceRequest<Node> object) async {
    // nothing to clone, left blank
  }

  @override
  Future<void> close() async {
    throw MethodException(ZX.ERR_NOT_SUPPORTED);
  }

  @override
  Future<Uint8List> query() async {
    throw UnsupportedError('ErrorNodeForSendingEvent.query is unreachable.');
  }

  @override
  Future<NodeInfoDeprecated> describeDeprecated() async {
    throw UnsupportedError(
        'ErrorNodeForSendingEvent.describeDeprecated is unreachable.');
  }

  @override
  Future<ConnectionInfo> getConnectionInfo() async {
    throw UnsupportedError(
        'ErrorNodeForSendingEvent.getConnectionInfo is unreachable.');
  }

  @override
  Future<Node$GetAttr$Response> getAttr() async {
    throw UnsupportedError('ErrorNodeForSendingEvent.getAttr is unreachable.');
  }

  @override
  Future<int> setAttr(
      NodeAttributeFlags flags, NodeAttributes attributes) async {
    return ZX.ERR_NOT_SUPPORTED;
  }

  @override
  Future<void> sync() async {
    throw MethodException(ZX.ERR_NOT_SUPPORTED);
  }

  @override
  Future<Node$GetFlags$Response> getFlags() async {
    return Node$GetFlags$Response(ZX.ERR_NOT_SUPPORTED, OpenFlags.$none);
  }

  @override
  Future<int> setFlags(OpenFlags flags) async {
    return ZX.ERR_NOT_SUPPORTED;
  }

  @override
  Future<Node$QueryFilesystem$Response> queryFilesystem() async {
    throw UnsupportedError(
        'ErrorNodeForSendingEvent.queryFilesystem is unreachable.');
  }
}
