// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class HandleWaiter {
  HandleWaiter(this._handle, this.signals) {
    if (_handle == null || _handle == MX_HANDLE_INVALID)
      throw new FidlInternalError('HandleWaiter requires a valid handle. Got handle: $_handle.');
    if (signals == null || signals == 0)
      throw new FidlInternalError('HandleWaiter requires non-zero signals. Got signals: $signals.');
    int status = MxHandle.registerFinalizer(this, _handle.h);
    if (status != NO_ERROR)
      throw new FidlInternalError('Failed to register the handle.');
  }

  final int signals;

  Handle _handle;
  RawReceivePort _receivePort;

  void start(void handler(int event)) {
    if (_receivePort != null)
      throw new FidlApiError('Already started: $this.');
    RawReceivePort receivePort = new RawReceivePort(handler);
    int status = HandleWatcher.add(_handle.h, receivePort.sendPort, signals);
    if (status != NO_ERROR)
      throw new FidlInternalError('Failed to watch handle: ${getStringForStatus(status)}');
    _receivePort = receivePort;
  }

  void next() {
    if (_receivePort == null)
      throw new FidlApiError('Not yet started: $this.');
    int status = HandleWatcher.add(_handle.h, _receivePort.sendPort, signals);
    if (status != NO_ERROR)
      throw new FidlInternalError('Failed to watch handle: ${getStringForStatus(status)}');
  }

  void stop() {
    if (_receivePort == null)
      throw new FidlApiError('Not yet started: $this.');
    HandleWatcher.remove(_handle.h);
    _receivePort.close();
    _receivePort = null;
  }

  @override
  String toString() => "HandleWaiter($_handle)";

  void close() {
    if (_handle == null)
      return;
    if (_receivePort != null) {
      MxHandle.removeOpenHandle(_handle.h);
      HandleWatcher.close(_handle.h);
      _receivePort.close();
      _receivePort = null;
    } else {
      _handle.close();
    }
    _handle = null;
  }
}
