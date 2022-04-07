// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_component_runner/fidl_async.dart' as fcrunner;
import 'package:fidl_fuchsia_diagnostics_types/fidl_async.dart' as fdiagtypes;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;

import 'package:fuchsia_logger/logger.dart';

import '../local_component_handles.dart';
import '../realm_builder.dart';

/// A LocalComponent holds the callbacks provided in [addLocalChild]. It
/// implements the local component's `ComponentController`, which is bound to
/// the interface when the component starts. Close the
/// [componentHandles.controllerBinding] (initialized on [run()]) to indicate
/// to Component Manager that the component has stopped.
class LocalComponent extends fcrunner.ComponentController {
  LocalComponentHandles? componentHandles;

  final onStopCompleter = Completer();

  /// The component name
  final String name;

  final OnRun onRun;
  final OnKill? onKill;
  final OnOnPublishDiagnostics? onOnPublishDiagnostics;
  final OnStop? onStop;

  /// Constructed from the [Function] parameters passed to [addLocalChild].
  LocalComponent(
    this.name,
    this.onRun,
    this.onKill,
    this.onOnPublishDiagnostics,
    this.onStop,
  );

  /// Invokes the [onKill] callback, if provided in [addLocalChild].
  @override
  Future<void> kill() {
    if (onKill != null) {
      if (componentHandles == null) {
        throw Exception(
          'LocalChild $name: ComponentController kill() unexpectedly called '
          'when componentHandles was null',
        );
      }
      return onKill!(componentHandles!);
    } else {
      log.info(
        'LocalChild $name: ComponentController.kill() was called '
        'but not implemented',
      );
      return Future.value();
    }
  }

  /// Invokes the [onOnPublishDiagnostics] callback, if provided in
  /// [addLocalChild].
  @override
  Stream<fdiagtypes.ComponentDiagnostics>? get onPublishDiagnostics {
    if (onOnPublishDiagnostics != null) {
      if (componentHandles == null) {
        throw Exception(
          'LocalChild $name: ComponentController kill() unexpectedly called '
          'when componentHandles was null',
        );
      }
      return onOnPublishDiagnostics!(componentHandles!);
    } else {
      return null;
    }
  }

  /// Invokes the [onStop] callback, if provided in [addLocalChild]. If not
  /// provided, then the default implementation will automatically close the
  /// [controllerBinding], to indicate to Component Manager that the
  /// [LocalComponent] has stopped. If an [onStop] callback is provided, that
  /// callback is responsible for closing the [controllerBinding].
  @override
  Future<void> stop() async {
    ensureStopCompleterIsCompleted();
    if (onStop != null) {
      if (componentHandles == null) {
        throw Exception(
          'LocalChild $name: ComponentController kill() unexpectedly called '
          'when componentHandles was null',
        );
      }
      await onStop!(componentHandles!);
      if (componentHandles != null &&
          !componentHandles!.controllerBinding.isClosed) {
        log.warning(
          'LocalChild $name: ComponentController.stop() was handled by supplied '
          '`onStop` closure, but the ComponentController channel was not '
          'closed. Typically, the component should close the '
          'ComponentController on `stop()`.',
        );
      }
    } else {
      if (componentHandles != null) {
        componentHandles!.close();
      }
    }
  }

  /// When the [LocalComponentRunner] gets a [start] request for a
  /// [LocalComponent], it binds the [ComponentController] and calls [run],
  /// invoking the [onRun] callback provided in [addLocalChild].
  Future<void> run(
    fidl.InterfaceRequest<fcrunner.ComponentController> controller,
    List<fcrunner.ComponentNamespaceEntry> namespace,
    fidl.InterfaceRequest<fio.Directory> outgoingDir,
  ) async {
    componentHandles = LocalComponentHandles(
      fcrunner.ComponentControllerBinding()..bind(this, controller),
      namespace,
      outgoingDir,
    );

    try {
      await onRun(componentHandles!, onStopCompleter);
      ensureStopCompleterIsCompleted();
    } on Exception catch (err, stacktrace) {
      log.severe('LocalChild $name: caught $err\n$stacktrace');
      ensureStopCompleterIsCompleted(error: err, stacktrace: stacktrace);
    }
    componentHandles!.close();
  }

  void ensureStopCompleterIsCompleted({Object? error, StackTrace? stacktrace}) {
    if (!onStopCompleter.isCompleted) {
      if (error != null) {
        onStopCompleter.completeError(error, stacktrace);
      } else {
        onStopCompleter.complete();
      }
    }
  }
}
