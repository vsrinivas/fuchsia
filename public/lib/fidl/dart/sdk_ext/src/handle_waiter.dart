// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of internal;

typedef void AsyncWaitCallback(int status, int pending);

class HandleWaiter extends NativeFieldWrapperClass2 {
  HandleWaiter() {
    String stack;
    assert(() {
      stack = StackTrace.current.toString();
      return true;
    });
    _constructor(stack);
  }
  void _constructor(String stack) native "HandleWaiter_constructor";

  void asyncWait(Handle handle, int signals, int timeout, AsyncWaitCallback callback) {
    _callback = callback;
    _asyncWait(handle.h, signals, timeout);
  }

  void cancelWait() {
    _callback = null;
    _cancelWait();
  }

  void _asyncWait(int handle, int signals, int timeout)
      native "HandleWaiter_asyncWait";

  void _cancelWait()
      native "HandleWaiter_cancelWait";

  void onWaitComplete(int status, int pending) {
    AsyncWaitCallback callback = _callback;
    _callback = null;
    callback(status, pending);
  }

  AsyncWaitCallback _callback;
}
