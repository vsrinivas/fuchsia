// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class Handle {
  int _h;
  int get h => _h;

  bool get isValid => _h != MX_HANDLE_INVALID;

  Handle(this._h, {String description}) {
    MxHandle.addOpenHandle(_h, description: description);
  }

  Handle.invalid() : _h = MX_HANDLE_INVALID;

  int close() {
    MxHandle.removeOpenHandle(_h);
    int result = MxHandle.close(_h);
    _h = MX_HANDLE_INVALID;
    return result;
  }

  Handle pass() {
    MxHandle.removeOpenHandle(_h);
    return this;
  }

  String toString() {
    return "Handle($h)";
  }

  bool operator ==(other) => (other is Handle) && (_h == other._h);

  int get hashCode => _h.hashCode;

  static bool reportLeakedHandles() => MxHandle.reportOpenHandles();
}
