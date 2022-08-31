// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl/fidl.dart' show InterfaceRequest, MethodException;
import 'package:fidl_fidl_test_compatibility/fidl_async.dart';
import 'package:fidl_fidl_test_imported/fidl_async.dart' as imported;
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

class EchoImpl extends Echo {
  final _binding = EchoBinding();
  final _echoEventStreamController = StreamController<Struct>();
  final _echoMinimalEventStreamController = StreamController<void>();
  final _onEchoNamedEventStreamController =
      StreamController<imported.SimpleStruct>();
  final _onEchoTablePayloadEventStreamController =
      StreamController<ResponseTable>();
  final _onEchoUnionPayloadEventStreamController =
      StreamController<ResponseUnion>();

  // Saves references to proxies from which we're expecting events.
  Map<String, EchoProxy> proxies = {};

  void bind(InterfaceRequest<Echo> request) {
    _binding.bind(this, request);
  }

  Future<Echo> proxy() async {
    final echo = EchoProxy();
    Incoming.fromSvcPath()..connectToService(echo);
    return echo;
  }

  @override
  Future<void> echoMinimal(String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoMinimal('');
    }
    return null;
  }

  @override
  Future<void> echoMinimalWithError(
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoMinimalWithError('', resultVariant);
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
      final echo = await proxy();
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
      return (await proxy()).echoStruct(value, '');
    }
    return value;
  }

  @override
  Future<Struct> echoStructWithError(Struct value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoStructWithError(value, err, '', resultVariant);
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
      final echo = await proxy();
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
      return (await proxy()).echoArrays(value, '');
    }
    return value;
  }

  @override
  Future<ArraysStruct> echoArraysWithError(ArraysStruct value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoArraysWithError(value, err, '', resultVariant);
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
      return (await proxy()).echoVectors(value, '');
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
      return (await proxy())
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
      return (await proxy()).echoTable(value, '');
    }
    return value;
  }

  @override
  Future<AllTypesTable> echoTableWithError(AllTypesTable value, DefaultEnum err,
      String forwardToServer, RespondWith resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoTableWithError(value, err, '', resultVariant);
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
      return (await proxy()).echoXunions(value, '');
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
      return (await proxy())
          .echoXunionsWithError(value, err, '', resultVariant);
    }
    if (resultVariant == RespondWith.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  @override
  Future<imported.SimpleStruct> echoNamedStruct(
      imported.SimpleStruct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoNamedStruct(value, '');
    }
    return value;
  }

  @override
  Future<imported.SimpleStruct> echoNamedStructWithError(
      imported.SimpleStruct value,
      int err,
      String forwardToServer,
      imported.WantResponse resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy())
          .echoNamedStructWithError(value, err, '', resultVariant);
    }
    if (resultVariant == imported.WantResponse.err) {
      throw MethodException(err);
    } else {
      return value;
    }
  }

  void _handleOnEchoNamedEvent(imported.SimpleStruct value, String serverUrl) {
    _onEchoNamedEventStreamController.add(value);
    // Not technically safe if there's more than one outstanding event on this
    // proxy, but that shouldn't happen in the existing test.
    proxies.remove(serverUrl);
  }

  @override
  Future<void> echoNamedStructNoRetVal(
      imported.SimpleStruct value, String forwardToServer) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      final echo = await proxy();
      // Keep echo around until we process the expected event.
      proxies[forwardToServer] = echo;
      echo.onEchoNamedEvent.listen((imported.SimpleStruct val) {
        _handleOnEchoNamedEvent(val, forwardToServer);
      });
      return echo.echoNamedStructNoRetVal(value, '');
    }
    return _onEchoNamedEventStreamController.add(value);
  }

  @override
  Stream<imported.SimpleStruct> get onEchoNamedEvent =>
      _onEchoNamedEventStreamController.stream;

  @override
  Future<ResponseTable> echoTablePayload(RequestTable payload) async {
    if (payload != null &&
        payload.forwardToServer != null &&
        payload.forwardToServer.isNotEmpty) {
      return (await proxy())
          .echoTablePayload(RequestTable(value: payload.value));
    }
    return ResponseTable(value: payload.value);
  }

  @override
  Future<ResponseTable> echoTablePayloadWithError(
      EchoEchoTablePayloadWithErrorRequest payload) async {
    if (payload != null &&
        payload.forwardToServer != null &&
        payload.forwardToServer.isNotEmpty) {
      return (await proxy()).echoTablePayloadWithError(
          EchoEchoTablePayloadWithErrorRequest(
              value: payload.value,
              resultErr: payload.resultErr,
              resultVariant: payload.resultVariant));
    }
    if (payload.resultVariant == RespondWith.err) {
      throw MethodException(payload.resultErr);
    } else {
      return ResponseTable(value: payload.value);
    }
  }

  void _handleOnEchoTablePayloadEvent(ResponseTable val, String serverUrl) {
    _onEchoTablePayloadEventStreamController.add(val);
    // Not technically safe if there's more than one outstanding event on this
    // proxy, but that shouldn't happen in the existing test.
    proxies.remove(serverUrl);
  }

  @override
  Future<void> echoTablePayloadNoRetVal(RequestTable payload) async {
    if (payload != null &&
        payload.forwardToServer != null &&
        payload.forwardToServer.isNotEmpty) {
      final echo = await proxy();
      // Keep echo around until we process the expected event.
      proxies[payload.forwardToServer] = echo;
      echo.onEchoTablePayloadEvent.listen((ResponseTable val) {
        _handleOnEchoTablePayloadEvent(val, payload.forwardToServer);
      });
      return echo.echoTablePayloadNoRetVal(RequestTable(value: payload.value));
    }
    return _onEchoTablePayloadEventStreamController
        .add(ResponseTable(value: payload.value));
  }

  @override
  Stream<ResponseTable> get onEchoTablePayloadEvent =>
      _onEchoTablePayloadEventStreamController.stream;

  @override
  Future<imported.SimpleStruct> echoTableRequestComposed(
      imported.ComposedEchoTableRequestComposedRequest payload) async {
    if (payload != null &&
        payload.forwardToServer != null &&
        payload.forwardToServer.isNotEmpty) {
      return (await proxy()).echoTableRequestComposed(
          imported.ComposedEchoTableRequestComposedRequest(
              value: payload.value));
    }
    return imported.SimpleStruct(f1: true, f2: payload.value);
  }

  @override
  Future<ResponseUnion> echoUnionPayload(RequestUnion payload) async {
    // Unsigned variant path.
    if (payload.$tag == RequestUnionTag.unsigned) {
      if (payload.unsigned.forwardToServer.isNotEmpty) {
        return (await proxy()).echoUnionPayload(RequestUnion.withUnsigned(
            Unsigned(value: payload.unsigned.value, forwardToServer: '')));
      }
      return ResponseUnion.withUnsigned(payload.unsigned.value);
    }

    // Signed variant path.
    if (payload.signed.forwardToServer.isNotEmpty) {
      return (await proxy()).echoUnionPayload(RequestUnion.withSigned(
          Signed(value: payload.signed.value, forwardToServer: '')));
    }
    return ResponseUnion.withSigned(payload.signed.value);
  }

  @override
  Future<ResponseUnion> echoUnionPayloadWithError(
      EchoEchoUnionPayloadWithErrorRequest payload) async {
    // Unsigned variant path.
    if (payload.$tag == EchoEchoUnionPayloadWithErrorRequestTag.unsigned) {
      if (payload.unsigned.forwardToServer.isNotEmpty) {
        return (await proxy()).echoUnionPayloadWithError(
            EchoEchoUnionPayloadWithErrorRequest.withUnsigned(UnsignedErrorable(
                value: payload.unsigned.value,
                forwardToServer: '',
                resultErr: payload.unsigned.resultErr,
                resultVariant: payload.unsigned.resultVariant)));
      }
      if (payload.unsigned.resultVariant == RespondWith.err) {
        throw MethodException(payload.unsigned.resultErr);
      } else {
        return ResponseUnion.withUnsigned(payload.unsigned.value);
      }
    }

    // Signed variant path.
    if (payload.signed.forwardToServer.isNotEmpty) {
      return (await proxy()).echoUnionPayloadWithError(
          EchoEchoUnionPayloadWithErrorRequest.withSigned(SignedErrorable(
              value: payload.signed.value,
              forwardToServer: '',
              resultErr: payload.signed.resultErr,
              resultVariant: payload.signed.resultVariant)));
    }
    if (payload.signed.resultVariant == RespondWith.err) {
      throw MethodException(payload.signed.resultErr);
    } else {
      return ResponseUnion.withSigned(payload.signed.value);
    }
  }

  void _handleOnEchoUnionPayloadEvent(ResponseUnion val, String serverUrl) {
    _onEchoUnionPayloadEventStreamController.add(val);
    // Not technically safe if there's more than one outstanding event on this
    // proxy, but that shouldn't happen in the existing test.
    proxies.remove(serverUrl);
  }

  @override
  Future<void> echoUnionPayloadNoRetVal(RequestUnion payload) async {
    // Unsigned variant path.
    if (payload.$tag == RequestUnionTag.unsigned) {
      if (payload.unsigned.forwardToServer.isNotEmpty) {
        final echo = await proxy();
        // Keep echo around until we process the expected event.
        proxies[payload.unsigned.forwardToServer] = echo;
        echo.onEchoUnionPayloadEvent.listen((ResponseUnion val) {
          _handleOnEchoUnionPayloadEvent(val, payload.unsigned.forwardToServer);
        });
        return echo.echoUnionPayloadNoRetVal(RequestUnion.withUnsigned(
            Unsigned(value: payload.unsigned.value, forwardToServer: '')));
      }
      return _onEchoUnionPayloadEventStreamController
          .add(ResponseUnion.withUnsigned(payload.unsigned.value));
    }

    // Signed variant path.
    if (payload.signed.forwardToServer.isNotEmpty) {
      final echo = await proxy();
      // Keep echo around until we process the expected event.
      proxies[payload.signed.forwardToServer] = echo;
      echo.onEchoUnionPayloadEvent.listen((ResponseUnion val) {
        _handleOnEchoUnionPayloadEvent(val, payload.signed.forwardToServer);
      });
      return echo.echoUnionPayloadNoRetVal(RequestUnion.withSigned(
          Signed(value: payload.signed.value, forwardToServer: '')));
    }
    return _onEchoUnionPayloadEventStreamController
        .add(ResponseUnion.withSigned(payload.signed.value));
  }

  @override
  Stream<ResponseUnion> get onEchoUnionPayloadEvent =>
      _onEchoUnionPayloadEventStreamController.stream;

  @override
  Future<imported.ComposedEchoUnionResponseWithErrorComposedResponse>
      echoUnionResponseWithErrorComposed(
          int value,
          bool wantAbsoluteValue,
          String forwardToServer,
          int resultErr,
          imported.WantResponse resultVariant) async {
    if (forwardToServer != null && forwardToServer.isNotEmpty) {
      return (await proxy()).echoUnionResponseWithErrorComposed(
          value, wantAbsoluteValue, '', resultErr, resultVariant);
    }

    if (resultVariant == imported.WantResponse.err) {
      throw MethodException(resultErr);
    } else if (wantAbsoluteValue) {
      return imported.ComposedEchoUnionResponseWithErrorComposedResponse
          .withUnsigned(value.abs());
    }
    return imported.ComposedEchoUnionResponseWithErrorComposedResponse
        .withSigned(value);
  }
}

void main(List<String> args) {
  final context = ComponentContext.create();
  final EchoImpl echoImpl = EchoImpl();
  context.outgoing
    ..addPublicService(echoImpl.bind, Echo.$serviceName)
    ..serveFromStartupInfo();
}
