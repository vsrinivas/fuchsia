// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/device_shell.fidl.dart';

/// Implements a DeviceShell for receiving the services a [DeviceShell] needs to
/// operate.  When [initialize] is called, the services it receives are routed
/// by this class to the various classes which need them.
class DeviceShellImpl extends DeviceShell {
  @override
  void terminate(void done()) => done();
}
