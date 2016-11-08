// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of internal;

// Data associated with an open handle.
class _OpenHandle {
  final StackTrace stack;
  String description;
  _OpenHandle(this.stack, {this.description});
}

class MxHandle {
  static HashMap<int, _OpenHandle> _openHandles = new HashMap();

  static void addOpenHandle(int handleToken, {String description}) {
    var stack;
    // We only remember a stack trace when in checked mode.
    assert((stack = StackTrace.current) != null);
    var openHandle = new _OpenHandle(stack, description: description);
    _openHandles[handleToken] = openHandle;
  }

  static void removeOpenHandle(int handleToken) {
    _openHandles.remove(handleToken);
  }

  static void _reportOpenHandle(int handle, _OpenHandle openHandle) {
    StringBuffer sb = new StringBuffer();
    sb.writeln('HANDLE LEAK: handle: $handle');
    if (openHandle.description != null) {
      sb.writeln('HANDLE LEAK: description: ${openHandle.description}');
    }
    if (openHandle.stack != null) {
      sb.writeln('HANDLE LEAK: creation stack trace:');
      sb.writeln(openHandle.stack);
    } else {
      sb.writeln('HANDLE LEAK: creation stack trace available in strict mode.');
    }
    print(sb.toString());
  }

  static bool reportOpenHandles() {
    if (_openHandles.length == 0) {
      return true;
    }
    _openHandles.forEach(_reportOpenHandle);
    return false;
  }

  static bool setDescription(int handleToken, String description) {
    _OpenHandle openHandle = _openHandles[handleToken];
    if (openHandle != null) {
      openHandle.description = description;
    }
    return true;
  }

  static int registerFinalizer(Object eventSubscription, int handleToken)
      native "MxHandle_RegisterFinalizer";

  static int close(int handleToken) native "MxHandle_Close";

  // Called from the embedder's unhandled exception callback.
  // Returns the number of successfully closed handles.
  static int _closeOpenHandles() {
    int count = 0;
    _openHandles.forEach((int handle, _) {
      if (MxHandle.close(handle) == 0) {
        count++;
      }
    });
    _openHandles.clear();
    return count;
  }
}

class _MxHandleWatcher {
  static int sendControlData(
      int controlHandle,
      int commandCode,
      int handleOrDeadline,
      SendPort port,
      int data) native "MxHandleWatcher_SendControlData";
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
