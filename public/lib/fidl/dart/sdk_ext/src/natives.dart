// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of internal;

class MxTime {
  static int _get(int clockId) native "MxTime_Get";

  // Clock ID zero is MX_CLOCK_MONOTONIC.
  static int timerMillisecondClock() => _get(0) ~/ (1000 * 1000);
}

class MxHandle {
  static int _registerFinalizer(Handle handle, int h)
      native "MxHandle_RegisterFinalizer";

  static int _unregisterFinalizer(Handle handle)
      native "MxHandle_UnregisterFinalizer";

  static int close(int h) native "MxHandle_Close";
}

/// A wrapper for an mx_Handle_t.
/// 
/// A Handle object registers a finalizer for its underlying mx_handle_t that
/// closes the mx_handle_t when the Handle is garbage collected.
class Handle extends NativeFieldWrapperClass2 {
  /// Creates a wrapper for the given mx_handle_t.
  Handle(this._h) {
    if (isValid)
      MxHandle._registerFinalizer(this, _h);
  }

  /// Creates a wrapper for MX_HANDLE_INVALID.
  Handle.invalid() : _h = 0;

  /// The mx_handle_t underlying this Handle object.
  int get h => _h;
  int _h;

  /// Returns true if [h] is not MX_HANDLE_INVALID.
  bool get isValid => _h != 0;

  /// Closes the underlying mx_handle_t and returns an mx_status_t.
  int close() {
    if (!isValid)
      return MxHandle.close(0);
    MxHandle._unregisterFinalizer(this);
    int result = MxHandle.close(_h);
    _h = 0;
    return result;
  }

  /// Returns the underlying mx_handle_t and sets [h] to MX_HANDLE_INVALID.
  /// 
  /// This function removes the finalizer for the underlying handle. It is the
  /// callee's responsibility to close the returned handle.
  int release() {
    if (!isValid)
      return 0;
    MxHandle._unregisterFinalizer(this);
    int result = _h;
    _h = 0;
    return result;
  }

  @override
  String toString() => 'Handle(${isValid ? _h : "MX_HANDLE_INVALID"})';

  @override
  bool operator ==(other) => (other is Handle) && (_h == other._h);

  @override
  int get hashCode => _h.hashCode;
}

class MxChannel {
  static List create(int flags) native "MxChannel_Create";

  static int write(int handleToken, ByteData data, int numBytes,
      List<int> handles, int flags) native "MxChannel_Write";

  static List read(int handleToken, ByteData data, int numBytes,
      List<int> handleTokens, int flags) native "MxChannel_Read";

  static void queryAndRead(int handleToken, int flags, List result)
      native "MxChannel_QueryAndRead";
}

int _environment;
int _outgoingServices;

class MxStartupInfo {
  static int takeEnvironment() {
    int handle = _environment;
    _environment = null;
    return handle;
  }

  static int takeOutgoingServices() {
    int handle = _outgoingServices;
    _outgoingServices = null;
    return handle;
  }
}
