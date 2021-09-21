// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

const kFizzBuzzUrl =
    'fuchsia-pkg://fuchsia.com/inspect-dart-codelab-fizzbuzz#meta/inspect-dart-codelab-fizzbuzz.cmx';

int counter = 0;

class CodelabEnvironment {
  ComponentControllerProxy _fizzBuzzController;
  ComponentControllerProxy _reverserController;
  EnvironmentProxy _childEnvironment;
  EnvironmentControllerProxy _childEnvironmentController;
  LauncherProxy _launcher;
  Channel _fizzbuzzOutDirectoryServer;
  bool _isCreated = false;
  String _label;

  String get label => _label;

  Future<void> create() async {
    if (_isCreated) {
      throw Exception("Can't create more than once");
    }

    final environmentProxy = EnvironmentProxy();
    final incoming = Incoming.fromSvcPath()..connectToService(environmentProxy);

    _childEnvironment = EnvironmentProxy();
    _childEnvironmentController = EnvironmentControllerProxy();

    final fizzBuzzServiceDirectory = DirectoryProxy();
    _fizzbuzzOutDirectoryServer =
        fizzBuzzServiceDirectory.ctrl.request().passChannel();
    final fizzbuzzOutDirectoryClient =
        fizzBuzzServiceDirectory.ctrl.unbind().passChannel();

    final additionalServices = ServiceList(
      names: [fidl_codelab.FizzBuzz.$serviceName],
      hostDirectory: fizzbuzzOutDirectoryClient,
    );

    final options = EnvironmentOptions(
      inheritParentServices: true,
      useParentRunners: true,
      deleteStorageOnDeath: true,
      killOnOom: true,
    );

    counter += 1;
    _label = 'codelab-$counter';
    await environmentProxy.createNestedEnvironment(
      _childEnvironment.ctrl.request(),
      _childEnvironmentController.ctrl.request(),
      _label,
      additionalServices,
      options,
    );

    _launcher = LauncherProxy();
    await _childEnvironment.getLauncher(_launcher.ctrl.request());

    await incoming.close();
    _isCreated = true;
  }

  Future<void> startFizzBuzz() async {
    _fizzBuzzController = await _startComponent(
      kFizzBuzzUrl,
      _fizzbuzzOutDirectoryServer,
    );
  }

  Future<fidl_codelab.ReverserProxy> startReverser(String reverserUrl) async {
    final outDirectory = Incoming();
    _reverserController = await _startComponent(
        reverserUrl, outDirectory.request().passChannel());

    final reverser = fidl_codelab.ReverserProxy();
    outDirectory.connectToService(reverser);
    return reverser;
  }

  Future<ComponentControllerProxy> _startComponent(
      String componentUrl, Channel outDirectoryRequest) async {
    final launchInfo = LaunchInfo(
      url: componentUrl,
      directoryRequest: outDirectoryRequest,
    );
    final controller = ComponentControllerProxy();
    await _launcher.createComponent(launchInfo, controller.ctrl.request());

    final completer = Completer();
    final listener = controller.onDirectoryReady.listen(completer.complete);
    await completer.future;
    await listener.cancel();

    return controller;
  }

  void dispose() {
    if (_reverserController != null) {
      _reverserController.ctrl.close();
    }
    if (_fizzBuzzController != null) {
      _fizzBuzzController.ctrl.close();
    }
    if (_childEnvironmentController != null) {
      _childEnvironmentController.ctrl.close();
    }
    if (_launcher != null) {
      _launcher.ctrl.close();
    }
    if (_childEnvironment != null) {
      _childEnvironment.ctrl.close();
    }
    _isCreated = false;
  }
}
