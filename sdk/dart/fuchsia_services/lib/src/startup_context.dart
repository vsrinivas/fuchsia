// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:zircon/zircon.dart';

import 'incoming.dart';
import 'internal/_startup_context_impl.dart';
import 'outgoing.dart';

/// Context information that this component received at startup.
///
/// The [StartupContext] holds references to the services and connections that
/// the component was launched with. Authors can use the startup context to
/// access useful information for connecting to other components and interacting
/// with the framework.
abstract class StartupContext {
  static StartupContext? _startupContext;

  /// Services that are available to this component.
  ///
  /// These services have been offered to this component by its parent or are
  /// ambiently offered by the Component Framework.
  final Incoming incoming;

  /// Services and data exposed to other components.
  ///
  /// Use [outgoing] to publish services and data to the component manager and
  /// other components.
  final Outgoing outgoing;

  /// Handle of the [ViewRef] of this component.
  ///
  /// Use [viewRef] to provide reference to this component's view.
  final Handle? viewRef;

  StartupContext(
      {required this.incoming, required this.outgoing, this.viewRef});

  /// Creates a startup context from the process startup info.
  ///
  /// Returns a cached [StartupContext] instance associated with the currently
  /// running component if one was already created.
  ///
  /// Authors should use this method of obtaining the [StartupContext] instead
  /// of instantiating one on their own as it will bind and connect to all the
  /// underlying services for them.
  factory StartupContext.fromStartupInfo() {
    return _startupContext ??= StartupContextImpl.fromStartupInfo();
  }
}
