// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:core';
import 'dart:typed_data';

import 'package:fidl/fidl.dart' show MethodException;
import 'package:fidl/fidl.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fidl_fidl_serversuite/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';
import 'package:fidl_zx/fidl_async.dart' show Rights;

class ClosedTargetImpl extends ClosedTarget {
  ClosedTargetImpl({ReporterProxy reporter, ClosedTargetBinding binding})
      : _reporter = reporter,
        _binding = binding;

  final ReporterProxy _reporter;
  final ClosedTargetBinding _binding;

  Future<void> oneWayNoPayload() async {
    await _reporter.receivedOneWayNoPayload();
  }

  Future<void> twoWayNoPayload() async {}
  Future<int> twoWayStructPayload(int v) async {
    return v;
  }

  Future<ClosedTargetTwoWayTablePayloadResponse> twoWayTablePayload(
      ClosedTargetTwoWayTablePayloadRequest payload) async {
    return ClosedTargetTwoWayTablePayloadResponse(v: payload.v);
  }

  Future<ClosedTargetTwoWayUnionPayloadResponse> twoWayUnionPayload(
      ClosedTargetTwoWayUnionPayloadRequest payload) async {
    if (payload.v == null) {
      throw ArgumentError("Request had an unknown union variant");
    }
    return ClosedTargetTwoWayUnionPayloadResponse.withV(payload.v);
  }

  Future<String> twoWayResult(ClosedTargetTwoWayResultRequest payload) async {
    if (payload.payload != null) {
      return payload.payload;
    } else if (payload.error != null) {
      throw MethodException(payload.error);
    } else {
      throw ArgumentError("Request had an unknown union variant");
    }
  }

  Future<Rights> getHandleRights(Handle handle) async {
    throw UnsupportedError(
        "Handle does not provide a method to get handle rights in Dart");
  }

  Future<Rights> getSignalableEventRights(Handle handle) {
    throw UnsupportedError(
        "Handle does not provide a method to get handle rights in Dart");
  }

  Future<Handle> echoAsTransferableSignalableEvent(Handle handle) async {
    return handle;
  }

  Future<void> closeWithEpitaph(int epitaphStatus) async {
    _binding.close(epitaphStatus);
  }

  Future<int> byteVectorSize(Uint8List vec) async {
    return vec.length;
  }

  Future<int> handleVectorSize(List<Handle> vec) async {
    return vec.length;
  }

  Future<Uint8List> createNByteVector(int n) async {
    print('returning $n byte vector');
    return Uint8List(n);
  }

  Future<List<Handle>> createNHandleVector(int n) async {
    throw UnsupportedError("Dart does not support creating zircon Events");
  }
}

class AjarTargetImpl extends AjarTargetServer {
  AjarTargetImpl({ReporterProxy reporter}) : _reporter = reporter;

  final ReporterProxy _reporter;

  Future<void> $unknownOneWay(int ordinal) async {
    await _reporter.receivedUnknownMethod(ordinal, UnknownMethodType.oneWay);
  }
}

class OpenTargetImpl extends OpenTargetServer {
  OpenTargetImpl({ReporterProxy reporter}) : _reporter = reporter;

  final ReporterProxy _reporter;
  final StreamController<void> _strictEvent = StreamController.broadcast();
  final StreamController<void> _flexibleEvent = StreamController.broadcast();

  Future<void> sendEvent(EventType eventType) async {
    if (eventType == EventType.strict) {
      _strictEvent.add(null);
    } else if (eventType == EventType.flexible) {
      _flexibleEvent.add(null);
    } else {
      throw ArgumentError(
          "Request had an unknown EventType variant ${eventType.$value}");
    }
  }

  Stream<void> get strictEvent => _strictEvent.stream;
  Stream<void> get flexibleEvent => _flexibleEvent.stream;
  Future<void> strictOneWay() async {
    await _reporter.receivedStrictOneWay();
  }

  Future<void> flexibleOneWay() async {
    await _reporter.receivedFlexibleOneWay();
  }

  Future<void> strictTwoWay() async {}
  Future<int> strictTwoWayFields(int replyWith) async {
    return replyWith;
  }

  Future<void> strictTwoWayErr(OpenTargetStrictTwoWayErrRequest payload) async {
    if (payload.replySuccess != null) {
      return;
    } else if (payload.replyError != null) {
      throw MethodException(payload.replyError);
    } else {
      throw ArgumentError(
          "Request had an unknown union variant ${payload.$ordinal}");
    }
  }

  Future<int> strictTwoWayFieldsErr(
      OpenTargetStrictTwoWayFieldsErrRequest payload) async {
    if (payload.replySuccess != null) {
      return payload.replySuccess;
    } else if (payload.replyError != null) {
      throw MethodException(payload.replyError);
    } else {
      throw ArgumentError(
          "Request had an unknown union variant ${payload.$ordinal}");
    }
  }

  Future<void> flexibleTwoWay() async {}
  Future<int> flexibleTwoWayFields(int replyWith) async {
    return replyWith;
  }

  Future<void> flexibleTwoWayErr(
      OpenTargetFlexibleTwoWayErrRequest payload) async {
    if (payload.replySuccess != null) {
      return;
    } else if (payload.replyError != null) {
      throw MethodException(payload.replyError);
    } else {
      throw ArgumentError(
          "Request had an unknown union variant ${payload.$ordinal}");
    }
  }

  Future<int> flexibleTwoWayFieldsErr(
      OpenTargetFlexibleTwoWayFieldsErrRequest payload) async {
    if (payload.replySuccess != null) {
      return payload.replySuccess;
    } else if (payload.replyError != null) {
      throw MethodException(payload.replyError);
    } else {
      throw ArgumentError(
          "Request had an unknown union variant ${payload.$ordinal}");
    }
  }

  Future<void> $unknownOneWay(int ordinal) async {
    await _reporter.receivedUnknownMethod(ordinal, UnknownMethodType.oneWay);
  }

  Future<void> $unknownTwoWay(int ordinal) async {
    await _reporter.receivedUnknownMethod(ordinal, UnknownMethodType.twoWay);
  }
}

class RunnerImpl extends Runner {
  Future<bool> isTestEnabled(Test test) async {
    switch (test) {
      // Dart does not currently have APIs for explicitly retrieving handle
      // rights, so getHandleRights and getSignalableEventRights cannot be
      // implemented.
      case Test.clientSendsTooManyRights:
      case Test.clientSendsTooFewRights:
      case Test.clientSendsWrongHandleType:
      case Test.clientSendsTooFewHandles:
      case Test.clientSendsObjectOverPlainHandle:
      // Dart does not currently have APIs for creating Fuchsia Event objects,
      // so the createNHandleVector method cannot be implemented.
      case Test.responseMatchesHandleLimit:
      case Test.responseExceedsHandleLimit:
      // These tests are currently out of spec and disabled across all bindings.
      case Test.badAtRestFlagsCausesClose:
      case Test.badDynamicFlagsCausesClose:
      // fxbug.dev/111260: Dart bindings send epitaphs incorrectly.
      case Test.serverSendsEpitaph:
      // fxbug.dev/111266: Dart bindings don't validate TXIDs.
      case Test.oneWayWithNonZeroTxid:
      case Test.twoWayNoPayloadWithZeroTxid:
      // fxbug.dev/111299: Dart bindings don't check for channel write errors,
      // so just ignore channel errors from sending too many bytes. This causes
      // the following tests to fail from various channel errors being ignored:
      // - Ignores the ZX_ERR_OUT_OF_RANGE from sending more than
      //   ZX_CHANNEL_MAX_MSG_BYTES
      case Test.responseExceedsByteLimit:
      // - Ignores the ZX_ERR_INVALID_ARGS from sending a handle which did not
      //   have the rights specified in the handle dispositions list.
      case Test.serverSendsTooFewRights:
        return false;
      default:
        return true;
    }
  }

  Future<void> start(
      InterfaceHandle<Reporter> reporterHandle, AnyTarget target) async {
    var reporter = ReporterProxy();
    reporter.ctrl.bind(reporterHandle);
    if (target.closedTarget != null) {
      var binding = ClosedTargetBinding();
      var server = ClosedTargetImpl(reporter: reporter, binding: binding);
      binding.bind(server, target.closedTarget);
    } else if (target.ajarTarget != null) {
      var binding = AjarTargetBinding();
      var server = AjarTargetImpl(reporter: reporter);
      binding.bind(server, target.ajarTarget);
    } else if (target.openTarget != null) {
      var binding = OpenTargetBinding();
      var server = OpenTargetImpl(reporter: reporter);
      binding.bind(server, target.openTarget);
    } else {
      throw ArgumentError("Unknown AnyTarget variant: ${target.$ordinal}");
    }
  }

  Future<void> checkAlive() async {}
}

ComponentContext _context;

void main(List<String> args) {
  setupLogger(name: 'fidl-dynsuite-dart-server');
  print('Dart server: main');
  _context = ComponentContext.create();

  _context.outgoing
    ..addPublicService<Runner>((request) {
      RunnerBinding().bind(RunnerImpl(), request);
    }, Runner.$serviceName)
    ..serveFromStartupInfo();
}
