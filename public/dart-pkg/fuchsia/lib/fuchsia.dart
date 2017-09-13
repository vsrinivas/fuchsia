// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fuchsia;

import 'dart:isolate';
import 'dart:zircon';

// ignore_for_file: native_function_body_in_non_sdk_code

Handle _environment;
Handle _outgoingServices;

class MxStartupInfo {
  static Handle takeEnvironment() {
    Handle handle = _environment;
    _environment = null;
    return handle;
  }

  static Handle takeOutgoingServices() {
    Handle handle = _outgoingServices;
    _outgoingServices = null;
    return handle;
  }
}

void _setReturnCode(int returnCode) native "SetReturnCode";

Error exit(int returnCode) {
  _setReturnCode(returnCode);
  Isolate.current.kill(priority: Isolate.IMMEDIATE);
}
