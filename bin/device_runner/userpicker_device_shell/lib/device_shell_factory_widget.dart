// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.modular.services.device/device_shell.fidl.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

/// A wrapper widget intended to be the root of the application that is
/// a [DeviceShellFactory].  Its main purpose is to hold the [applicationContext] and
/// [DeviceShellFactory] instances so they aren't garbage collected.
/// For convienence, [advertise] does the advertising of the app as a
/// [DeviceShellFactory] to the rest of the system via the [applicationContext].
class DeviceShellFactoryWidget extends StatelessWidget {
  /// The [ApplicationContext] to [advertise] its [DeviceShellFactory] services to.
  final ApplicationContext applicationContext;

  /// The [DeviceShellFactory] to [advertise].
  final DeviceShellFactory deviceShellFactory;

  /// The rest of the application.
  final Widget child;

  final DeviceShellFactoryBinding _binding = new DeviceShellFactoryBinding();

  /// Constructor.
  DeviceShellFactoryWidget(
      {this.applicationContext, this.deviceShellFactory, this.child});

  @override
  Widget build(BuildContext context) => child;

  /// Advertises [deviceShellFactory] as a [DeviceShellFactory] to the rest of the system via
  /// the [applicationContext].
  void advertise() => applicationContext.outgoingServices.addServiceForName(
        (InterfaceRequest<DeviceShellFactory> request) =>
            _binding.bind(deviceShellFactory, request),
        DeviceShellFactory.serviceName,
      );
}
