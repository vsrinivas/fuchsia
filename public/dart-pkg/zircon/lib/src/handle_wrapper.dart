// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

/// A base class for classes that wrap Handles.
class _HandleWrapper<T> {
  Handle _handle;
  _HandleWrapper(this._handle);
  Handle get handle => _handle;

  void close() {
    _handle.close();
    _handle = null;
  }

  @override
  String toString() => '$runtimeType($handle)';
}

/// A base class for classes that wrap a pair of Handles.
abstract class _HandleWrapperPair<T> {
  final int status;
  final T first;
  final T second;
  _HandleWrapperPair._(this.status, this.first, this.second);
}
