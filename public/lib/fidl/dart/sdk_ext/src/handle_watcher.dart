// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of internal;

class _MxHandleWatcher {
  static int sendControlData(
      int controlHandle,
      int commandCode,
      int handleOrDeadline,
      SendPort port,
      int data) native "MxHandleWatcher_SendControlData";
}

class HandleWatcher {
  // Control commands.
  static const int _kAdd = 0;
  static const int _kRemove = 1;
  static const int _kClose = 2;
  static const int _kTimer = 3;
  static const int _kShutdown = 4;

  static const int _kHandleInvalid = 0;

  // This value is from //magenta/system/public/magenta/errors.h
  static const int _kErrBadState = -20;

  static int controlHandle;

  static int _sendControlData(int command,
                              int handleOrDeadline,
                              SendPort port,
                              int signals) {
    final int localControlHandle = controlHandle;
    if (localControlHandle == _kHandleInvalid)
      return _kErrBadState;
    return _MxHandleWatcher.sendControlData(
        localControlHandle, command, handleOrDeadline, port, signals);
  }

  static int close(int handle) {
    return _sendControlData(_kClose, handle, null, 0);
  }

  static int add(int handle, SendPort port, int signals) {
    return _sendControlData(_kAdd, handle, port, signals);
  }

  static int remove(int handle) {
    return _sendControlData(_kRemove, handle, null, 0);
  }

  static int timer(Object ignored, SendPort port, int deadline) {
    // The deadline will be unwrapped before sending to the handle watcher.
    return _sendControlData(_kTimer, deadline, port, 0);
  }
}
