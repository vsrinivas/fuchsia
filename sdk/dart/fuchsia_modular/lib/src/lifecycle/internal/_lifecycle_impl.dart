// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fuchsia_services/services.dart';

import '../lifecycle.dart';

/// Allows running components to be notified when lifecycle events happen in the
/// system.
///
/// This class is not intended to be used directly by authors but instead should
/// be used by the [Lifecycle] factory constructor. This class is the default
/// implementation that will be used by [Lifecycle] if no other implementation
/// is provided. We do so to obfuscate the actual implementation to avoid
/// exposing too much to the user, aid with testing and to prevent users from
/// subclassing this class.
///
/// Note: This class must be exposed to the framework before the first iteration
/// of the event loop. Therefore, it must be initialized by the time the Module
/// or Agent is initialized.
class LifecycleImpl extends fidl.Lifecycle implements Lifecycle {
  /// The underlying [Binding] that connects this to client requests.
  final fidl.LifecycleBinding _lifecycleBinding = fidl.LifecycleBinding();

  // A set of all registered terminate listeners
  final _terminateListeners = <Future<void> Function()>{};

  /// A function which will be invoked after all of the terminateListeners
  /// are executed.
  final void Function(int)? exitHandler;

  /// Initializes this [LifecycleImpl] instance
  LifecycleImpl({required Outgoing outgoing, this.exitHandler}) {
    _exposeService(outgoing);
  }

  /// Adds a terminate [listener] which will be called when the system starts to
  /// shutdown this process. Returns `false` if this listener was already added.
  @override
  bool addTerminateListener(Future<void> Function()? listener) {
    if (listener == null) {
      throw Exception('listener cannot be null!');
    }
    return _terminateListeners.add(listener);
  }

  // This is being triggered by the framework when the system starts to shutdown
  // this process.
  @override
  Future<void> terminate() async {
    // close the underlying binding
    _lifecycleBinding.close();
    // terminate all registered listeners
    await Future.wait(_terminateListeners.map((h) => h()));
    // if we don't terminate ourselves in a timely manner, we will be
    // forcibly terminated by the system.
    if (exitHandler != null) {
      exitHandler!(0);
    }
  }

  // Exposes this instance to the [Outgoing] directory.
  //
  // This class be must called before the outgoing directory is served.
  void _exposeService(Outgoing outgoing) {
    outgoing.addPublicService(
      (InterfaceRequest<fidl.Lifecycle> request) {
        _lifecycleBinding.bind(this, request);
      },
      fidl.Lifecycle.$serviceName,
    );
  }
}
