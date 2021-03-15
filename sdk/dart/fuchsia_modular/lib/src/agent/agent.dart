// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fuchsia_services/services.dart';

import 'internal/_agent_impl.dart';

/// The service provider function that is responsible to return a service that
/// will be exposed upon receiving a service request. Where [T] represents the
/// service type.
typedef ServiceProvider<T extends Service> = FutureOr<T>? Function();

/// Agent is a globally available object which simplifies common tasks that
/// agent developers will face. At a high level, it is a wrapper around the
/// [agent_context.fidl] and [agent.fidl] interface.
abstract class Agent {
  static Agent? _agent;

  /// Initializes the shared [Agent] instance.
  factory Agent() {
    return _agent ??= AgentImpl();
  }

  /// Associate [serviceImpl] to this [Agent] and exposes it to the rest of the
  /// system so that it can be discovered and connected to. Notice that
  /// [serviceImpl] is of type `FutureOr<T>`, where [T] represents the service
  /// type, to enable the ability to wait for any asynchronous operations to
  /// finish before initializing and exposing the service.
  ///
  /// Note: Multiple connections will be allowed to this [serviceImpl].
  ///
  /// Usage example:
  /// ```
  /// import 'package:fidl_fuchsia_foo/fidl_async.dart' as fidl;
  /// import 'package:fuchsia_modular/agent.dart';
  /// import 'src/foo_service_impl.dart';
  ///
  /// void main(List<String> args) {
  ///   final context = ComponentContext.create();
  ///   Agent()..exposeService(FooServiceImpl())..serve(context.outgoing);
  ///   context.outgoing.serveFromStartupInfo();
  /// }
  ///
  /// class FooServiceImpl extends fidl.FooService { ... }
  /// ```
  void exposeService<T extends Service>(FutureOr<T> serviceImpl);

  /// Similar to [#exposeService] but instead of passing the service
  /// implementation directly, pass a provider function that can be invoked
  /// asynchronously, when a request is received, to provide the service
  /// implementation at run time.
  ///
  /// [serviceData] can be found as part of the generated FIDL bindings, it
  /// holds the service runtime name and bindings object used for establishing a
  /// connection.
  ///
  /// [ServiceProvider] is defined as follows:
  /// ```
  /// typedef ServiceProvider<T> = FutureOr<T> Function();
  /// ```
  /// Where [T] represents the service type.
  ///
  /// Usage example:
  /// ```
  /// import 'package:fidl_fuchsia_foo/fidl_async.dart' as fidl;
  /// import 'package:fuchsia_modular/agent.dart';
  /// import 'src/foo_service_impl.dart';
  ///
  /// void main(List<String> args) {
  ///   final context = ComponentContext.create();
  ///   Agent()
  ///       ..exposeServiceProvider(getService, fidl.FooServiceData())
  ///       ..serve(context.outgoing);
  ///   context.outgoing.serveFromStartupInfo();
  /// }
  ///
  /// FutureOr<FooServiceImpl> getService() {
  ///   // do something fancy here
  ///   return FooServiceImpl();
  /// }
  ///
  /// class FooServiceImpl extends fidl.FooService { ... }
  /// ```
  void exposeServiceProvider<T extends Service>(
      ServiceProvider<T> serviceProvider, ServiceData<T> serviceData);

  /// Exposes the Agent's [fidl.Agent] instance to the Outgoing directory. In
  /// other words, advertises this as an [fidl.Agent] to the rest of the system
  /// via [Outgoing].
  ///
  /// This class should be called before the Outgoing directory is served.
  void serve(Outgoing outgoing);
}
