// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/device_context.fidl.dart';
import 'package:apps.modular.services.device/device_shell.fidl.dart';
import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

typedef void OnUserProviderReceived(UserProvider userProvider);
typedef void OnDeviceContextReceived(DeviceContext deviceContext);

/// Implements a DeviceShell for receiving the services a [DeviceShellFactory] needs to
/// operate.  When [create] is called, the services it receives are routed
/// by this class to the various classes which need them.
class DeviceShellFactoryImpl extends DeviceShellFactory {
  final DeviceContextProxy _deviceContextProxy = new DeviceContextProxy();
  final UserProviderProxy _userProviderProxy = new UserProviderProxy();
  final DeviceShellBinding _deviceShellBinding = new DeviceShellBinding();

  final DeviceShell deviceShell;
  final OnUserProviderReceived onUserProviderReceived;
  final OnDeviceContextReceived onDeviceContextReceived;

  DeviceShellFactoryImpl({
    this.deviceShell,
    this.onUserProviderReceived,
    this.onDeviceContextReceived,
  });

  @override
  void create(
    InterfaceHandle<DeviceContext> deviceContext,
    InterfaceHandle<UserProvider> userProvider,
    InterfaceRequest<DeviceShell> deviceShellRequest,
  ) {
    _deviceContextProxy.ctrl.bind(deviceContext);
    onDeviceContextReceived?.call(_deviceContextProxy);

    _userProviderProxy.ctrl.bind(userProvider);
    onUserProviderReceived?.call(_userProviderProxy);

    _deviceShellBinding.bind(deviceShell, deviceShellRequest);
  }
}
