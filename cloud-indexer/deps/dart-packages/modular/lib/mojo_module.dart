// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:mojo/application.dart';
import 'package:mojo/core.dart';

import 'modular/module.mojom.dart' as mojom;
import 'src/module_impl.dart';
import 'state_graph.dart';

export 'representation_types.dart' show bindingsRegistry;

/// Interface for Mojo-based Modules.
///
/// Contains methods for persisting and restoring state.
abstract class MojoModule {
  /// Notifies the Module that it has been initialized. Invoked exactly once
  /// before any calls to onChange().
  void onInitialize() {}

  /// Notifies the Module of a change in its state. The given [StateGraph] is
  /// valid to use until the next call to onChange(). No new invocations are
  /// made until the [Future] returned by the previous one returns.
  Future<Null> onChange(StateGraph state);
}

typedef MojoModule ModuleDelegateFactory();

/// Mojo [Application] that exposes the Module implementation as a Mojo service.
class ModuleApplication extends Application {
  final ModuleDelegateFactory _delegateFactory;

  /// Creates a new [ModuleApplication]. [_verb] is used as the url of the Mojo
  /// service exposing the module implementation. [_delegateFactory] will be
  /// called to create instances of the module delegate for each instance of the
  /// service.
  ModuleApplication(this._delegateFactory, final MojoHandle shellHandle)
      : super.fromHandle(shellHandle);

  @override
  void initialize(final List<String> args, final String url) {}

  @override
  void acceptConnection(final String requestorUrl, final String resolvedUrl,
      final ApplicationConnection connection) {
    connection.provideService(mojom.Module.serviceName,
        (MojoMessagePipeEndpoint endpoint) {
      final MojoModule delegate = _delegateFactory();
      new ModuleImpl(endpoint, delegate,
          moduleTag: delegate.runtimeType.toString());
    });
  }
}
