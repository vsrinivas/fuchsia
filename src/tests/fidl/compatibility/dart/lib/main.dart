// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' show InterfaceRequest, MethodException;
import 'package:fidl_fidl_test_compatibility/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

class EchoImpl extends Echo {
  final _binding = EchoBinding();
  final _echoEventStreamController = StreamController<Struct>();
  final _echoMinimalEventStreamController = StreamController<void>();

  // Saves references to proxies from which we're expecting events.
  Map<String, EchoProxy> proxies = {};

  void bind(InterfaceRequest<Echo> request) {
    _binding.bind(this, request);
  }

  Future<Echo> proxy(String url) async {
    assert(url.isNotEmpty);
    final incoming = Incoming();
    final launchInfo = LaunchInfo(
        url: url, directoryRequest: incoming.request().passChannel());
    final controller = ComponentControllerProxy();
    final launcher = LauncherProxy();
    Incoming.fromSvcPath().connectToService(launcher);
    await launcher.createComponent(launchInfo, controller.ctrl.request());
    final echo = EchoProxy();
    incoming.connectToService(echo);
    return echo;
  }

  @override
  Future<void> echoMinimal(String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoMinimal('');
    }
    return null;
  }

  @override
  Future<void> echoMinimalWithError(
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoMinimalWithError('', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(0);
    } else {
      return null;
    }
  }

  void _handleEchoMinimalEvent(String serverUrl) {
    _echoMinimalEventStreamController.add(null);
    // Not technically safe if there's more than one outstanding event on this
    // proxy, but that shouldn't happen in the existing test.
    proxies.remove(serverUrl);
  }

  @override
  Future<void> echoMinimalNoRetVal(String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      final echo = await proxy(forwardToServer);
      // Keep echo around until we process the expected event.
      proxies[forwardToServer] = echo;
      echo.echoMinimalEvent.listen((v) {
        _handleEchoMinimalEvent(forwardToServer);
      });
      return echo.echoMinimalNoRetVal('');
    }
    return _echoMinimalEventStreamController.add(null);
  }

  @override
  Stream<void> get echoMinimalEvent => _echoMinimalEventStreamController.stream;

  @override
  Future<Struct> echoStruct(Struct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoStruct(value, '');
    }
    return value;
  }

  @override
  Future<Struct> echoStructWithError(Struct value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoStructWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  void _handleEchoEvent(Struct value, String serverUrl) {
    _echoEventStreamController.add(value);
    // Not technically safe if there's more than one outstanding event on this
    // proxy, but that shouldn't happen in the existing test.
    proxies.remove(serverUrl);
  }

  @override
  Future<void> echoStructNoRetVal(Struct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      final echo = await proxy(forwardToServer);
      // Keep echo around until we process the expected event.
      proxies[forwardToServer] = echo;
      echo.echoEvent.listen((Struct val) {
        _handleEchoEvent(val, forwardToServer);
      });
      return echo.echoStructNoRetVal(value, '');
    }
    return _echoEventStreamController.add(value);
  }

  @override
  Stream<Struct> get echoEvent => _echoEventStreamController.stream;

  @override
  Future<ArraysStruct> echoArrays(
      ArraysStruct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoArrays(value, '');
    }
    return value;
  }

  @override
  Future<ArraysStruct> echoArraysWithError(ArraysStruct value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoArraysWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  @override
  Future<VectorsStruct> echoVectors(
      VectorsStruct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoVectors(value, '');
    }
    return value;
  }

  @override
  Future<VectorsStruct> echoVectorsWithError(
      VectorsStruct value,
      DefaultEnum err,
      String forwardToServer,
      RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoVectorsWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  @override
  Future<AllTypesTable> echoTable(
      AllTypesTable value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoTable(value, '');
    }
    return value;
  }

  @override
  Future<AllTypesTable> echoTableWithError(AllTypesTable value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoTableWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  @override
  Future<List<AllTypesXunion>> echoXunions(
      List<AllTypesXunion> value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer)).echoXunions(value, '');
    }
    return value;
  }

  @override
  Future<List<AllTypesXunion>> echoXunionsWithError(
      List<AllTypesXunion> value,
      DefaultEnum err,
      String forwardToServer,
      RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy(forwardToServer))
          .echoXunionsWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }
}

void main(List<String> args) {
  final context = ComponentContext.create();
  final EchoImpl echoImpl = EchoImpl();
  context.outgoing
    ..addPublicService(echoImpl.bind, Echo.$serviceName)
    ..serveFromStartupInfo();
}
