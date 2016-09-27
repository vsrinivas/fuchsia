// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:uuid/uuid.dart';

abstract class _Id {
  final String _id;
  const _Id(this._id);
  @override // Object
  String toString() => _id;

  @override // Object
  int get hashCode => _id.hashCode;
  @override // Object
  bool operator ==(other) =>
      other is _Id && other.runtimeType == runtimeType && other._id == _id;
}

class NodeId extends _Id {
  NodeId.fromString(String id) : super(id);
}

class EdgeId extends _Id {
  EdgeId.fromString(String id) : super(id);
}

/// Callbacks that return IDs that must not be reused by another node/edge
/// in the graph in the future.
typedef NodeId NodeIdGenerator();
typedef EdgeId EdgeIdGenerator();

class PrefixNodeIdGenerator {
  final Uuid _uuid = new Uuid();
  final String _prefix;
  PrefixNodeIdGenerator(this._prefix);
  NodeId call() {
    return _prefix != null
        ? new NodeId.fromString('node:$_prefix:${_uuid.v4()}')
        : new NodeId.fromString('node:${_uuid.v4()}');
  }
}

class PrefixEdgeIdGenerator {
  final Uuid _uuid = new Uuid();
  final String _prefix;
  PrefixEdgeIdGenerator(this._prefix);
  EdgeId call() {
    return _prefix != null
        ? new EdgeId.fromString('edge:$_prefix:${_uuid.v4()}')
        : new EdgeId.fromString('edge:${_uuid.v4()}');
  }
}
