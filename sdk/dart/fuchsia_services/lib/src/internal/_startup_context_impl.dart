// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart' as fidl_io;
import 'package:fidl_fuchsia_sys/fidl_async.dart' as fidl_sys;
import 'package:fuchsia/fuchsia.dart';
import 'package:zircon/zircon.dart';

import '../incoming.dart';
import '../outgoing.dart';
import '../startup_context.dart';

// ignore_for_file: prefer_constructors_over_static_methods, public_member_api_docs

/// A concrete implementation of the [StartupContext] interface.
///
/// This class is not intended to be used directly by authors but instead
/// should be used by the [StartupContext] factory constructor.
class StartupContextImpl implements StartupContext {
  static const String _serviceRootPath = '/svc';

  /// Services that are available to this component.
  ///
  /// These services have been offered to this component by its parent or are
  /// ambiently offered by the Component Framework.
  @override
  final Incoming incoming;

  /// Services and data exposed to other components.
  ///
  /// Use [outgoing] to publish services and data to the component manager and
  /// other components.
  @override
  final Outgoing outgoing;

  /// Creates a new instance of [StartupContext].
  ///
  /// This constructor is rarely used directly. Instead, most clients create a
  /// startup context using [StartupContext.fromStartupInfo].
  StartupContextImpl({
    required this.incoming,
    required this.outgoing,
  });

  /// Creates a startup context from the process startup info.
  ///
  /// Returns a cached [StartupContext] instance associated with the currently
  /// running component if one was already created.
  ///
  /// Authors should use this method of obtaining the [StartupContext] instead
  /// of instantiating one on their own as it will bind and connect to all the
  /// underlying services for them.
  factory StartupContextImpl.fromStartupInfo() {
    if (Platform.isFuchsia) {
      // Note takeOutgoingServices shouldn't be called more than once per pid
      final outgoingServicesHandle = MxStartupInfo.takeOutgoingServices();

      return StartupContextImpl(
        incoming: Incoming.fromSvcPath(),
        outgoing: _getOutgoingFromHandle(outgoingServicesHandle),
      );
    }

    // The following is required to enable host side tests.
    return StartupContextImpl(
      incoming: Incoming(),
      outgoing: Outgoing(),
    );
  }

  /// Creates a startup context from [fidl_sys.StartupInfo].
  ///
  /// Typically used for testing or by implementations of [fidl_sys.Runner] to
  /// obtain the [StartupContext] for components being run by the runner.
  factory StartupContextImpl.from(fidl_sys.StartupInfo startupInfo) {
    final flat = startupInfo.flatNamespace;
    if (flat.paths.length != flat.directories.length) {
      throw Exception('The flat namespace in the given fuchsia.sys.StartupInfo '
          '[$startupInfo] is misconfigured');
    }
    Channel? serviceRoot;
    for (var i = 0; i < flat.paths.length; ++i) {
      if (flat.paths[i] == _serviceRootPath) {
        serviceRoot = flat.directories[i];
        break;
      }
    }

    final dirProxy = fidl_io.DirectoryProxy()
      ..ctrl.bind(InterfaceHandle(serviceRoot));
    final incomingSvc = Incoming.withDirectory(dirProxy);

    Channel? dirRequestChannel = startupInfo.launchInfo.directoryRequest;

    return StartupContextImpl(
      incoming: incomingSvc,
      outgoing: _getOutgoingFromChannel(dirRequestChannel),
    );
  }

  static Outgoing _getOutgoingFromHandle(Handle outgoingServicesHandle) {
    final outgoingServices = Outgoing()
      ..serve(InterfaceRequest<fidl_io.Node>(Channel(outgoingServicesHandle)));
    return outgoingServices;
  }

  static Outgoing _getOutgoingFromChannel(Channel? directoryRequestChannel) {
    if (directoryRequestChannel == null) {
      throw ArgumentError.notNull('directoryRequestChannel');
    }
    final outgoingServices = Outgoing()
      ..serve(InterfaceRequest<fidl_io.Node>(directoryRequestChannel));
    return outgoingServices;
  }
}
