// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Base class for invalid node exceptions.
class _InvalidNodeException implements Exception {
  final int nodeId;

  const _InvalidNodeException(this.nodeId);
}

/// Exception returned when the node requested by a module is of the wrong type.
class InvalidNodeTypeException extends _InvalidNodeException {
  const InvalidNodeTypeException(final int nodeId) : super(nodeId);
}

/// Exception returned when the node index requested by a module is unknown.
class InvalidNodeIndexException extends _InvalidNodeException {
  const InvalidNodeIndexException(final int nodeId) : super(nodeId);
}

/// Exception returned when the node id requested by a module is unknown.
class InvalidNodeIDException extends _InvalidNodeException {
  const InvalidNodeIDException(final int nodeId) : super(nodeId);
}

/// Exception returned when a new representation value is added to a node which
/// already has one. One should delete and create a new node instead.
class ModifiedRepresentationNodeException extends _InvalidNodeException {
  const ModifiedRepresentationNodeException(final int nodeId) : super(nodeId);
}

/// Exception returned when a module tries to write an edge that doesn't match
/// its declared outputs.
class UnauthorizedEdgeException implements Exception {
  const UnauthorizedEdgeException();
}

/// Base exception that contains a message.
abstract class _Exception implements Exception {
  final String message;

  _Exception(this.message);

  @override
  String toString() {
    return "$runtimeType: $message";
  }
}

/// Exception raised when an authentication error happens.
class AuthenticationException extends _Exception {
  AuthenticationException(final String message) : super(message);
}

/// Exception raise when an unexpected error happens when using the ledger.
class LedgerGraphException extends _Exception {
  LedgerGraphException(final String message) : super(message);
}
