// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' show MethodException;
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

Duration durationFromSeconds(double seconds) =>
    Duration(microseconds: (seconds * Duration.microsecondsPerSecond).round());

class TestServerImpl extends TestServer {
  bool _receivedOneWayNoArgs = false;

  @override
  Future<void> oneWayNoArgs() async {
    _receivedOneWayNoArgs = true;
  }

  @override
  Future<bool> receivedOneWayNoArgs() async {
    return _receivedOneWayNoArgs;
  }

  String _oneWayStringArg;

  @override
  Future<void> oneWayStringArg(String value) async {
    _oneWayStringArg = value;
  }

  @override
  Future<String> receivedOneWayString() async {
    return _oneWayStringArg;
  }

  int _oneWayThreeArgX;
  int _oneWayThreeArgY;
  NoHandleStruct _oneWayThreeArgZ;

  @override
  Future<void> oneWayThreeArgs(int x, int y, NoHandleStruct z) async {
    _oneWayThreeArgX = x;
    _oneWayThreeArgY = y;
    _oneWayThreeArgZ = z;
  }

  @override
  Future<TestServer$ReceivedOneWayThreeArgs$Response>
      receivedOneWayThreeArgs() async {
    return TestServer$ReceivedOneWayThreeArgs$Response(
        _oneWayThreeArgX, _oneWayThreeArgY, _oneWayThreeArgZ);
  }

  ExampleTable _oneWayExampleTable;

  @override
  Future<void> oneWayExampleTable(ExampleTable value) async {
    _oneWayExampleTable = value;
  }

  @override
  Future<ExampleTable> receivedOneWayExampleTable() async {
    return _oneWayExampleTable;
  }

  ExampleXunion _oneWayExampleXunion;

  @override
  Future<void> oneWayExampleXunion(ExampleXunion value) async {
    _oneWayExampleXunion = value;
  }

  @override
  Future<ExampleXunion> receivedOneWayExampleXunion() async {
    return _oneWayExampleXunion;
  }

  ExampleBits _oneWayExampleBits;

  @override
  Future<void> oneWayExampleBits(ExampleBits value) async {
    _oneWayExampleBits = value;
  }

  @override
  Future<ExampleBits> receivedOneWayExampleBits() async {
    return _oneWayExampleBits;
  }

  @override
  Future<void> twoWayNoArgs() async {}

  @override
  Future<String> twoWayStringArg(String value) async {
    return value;
  }

  @override
  Future<TestServer$TwoWayThreeArgs$Response> twoWayThreeArgs(
      int x, int y, NoHandleStruct z) async {
    return TestServer$TwoWayThreeArgs$Response(x, y, z);
  }

  final StreamController<void> _emptyEventController =
      StreamController.broadcast();
  @override
  Future<void> sendEmptyEvent() async {
    _emptyEventController.add(null);
  }

  @override
  Stream<void> get emptyEvent => _emptyEventController.stream;

  final StreamController<String> _stringEventController =
      StreamController.broadcast();
  @override
  Future<void> sendStringEvent(String value) async {
    _stringEventController.add(value);
  }

  @override
  Stream<String> get stringEvent => _stringEventController.stream;

  final StreamController<TestServer$ThreeArgEvent$Response>
      _threeArgEventController = StreamController.broadcast();

  @override
  Future<void> sendThreeArgEvent(int x, int y, NoHandleStruct z) async {
    _threeArgEventController.add(TestServer$ThreeArgEvent$Response(x, y, z));
  }

  @override
  Stream<TestServer$ThreeArgEvent$Response> get threeArgEvent =>
      _threeArgEventController.stream;

  final StreamController<int> _multipleEventController =
      StreamController.broadcast();
  @override
  Future<void> sendMultipleEvents(int count, double intervalSeconds) async {
    if (intervalSeconds == 0.0) {
      _binding.close();
    } else {
      int index = 0;
      Timer.periodic(durationFromSeconds(intervalSeconds), (timer) {
        index++;
        _multipleEventController.add(index);
        if (index >= count) {
          timer.cancel();
        }
      });
    }
  }

  @override
  Stream<int> get multipleEvent => _multipleEventController.stream;

  @override
  Future<String> replySlowly(String value, double delaySeconds) {
    return Future.delayed(durationFromSeconds(delaySeconds), () => value);
  }

  @override
  Future<void> replyWithErrorZero(bool withError) async {
    if (withError) {
      throw MethodException(23);
    }
  }

  @override
  Future<String> replyWithErrorOne(bool withError, String value) async {
    if (withError) {
      throw MethodException(42);
    } else {
      return value;
    }
  }

  @override
  Future<TestServer$ReplyWithErrorMore$Response> replyWithErrorMore(
      bool withError, String value, bool otherValue) async {
    if (withError) {
      throw MethodException(666);
    } else {
      return TestServer$ReplyWithErrorMore$Response(value, otherValue);
    }
  }

  @override
  Future<void> replyWithErrorEnumZero(bool withError) async {
    if (withError) {
      throw MethodException(EnumOne.one);
    }
  }

  @override
  Future<String> replyWithErrorEnumOne(bool withError, String value) async {
    if (withError) {
      throw MethodException(EnumOne.two);
    } else {
      return value;
    }
  }

  @override
  Future<TestServer$ReplyWithErrorEnumMore$Response> replyWithErrorEnumMore(
      bool withError, String value, bool otherValue) async {
    if (withError) {
      throw MethodException(EnumOne.three);
    } else {
      return TestServer$ReplyWithErrorEnumMore$Response(value, otherValue);
    }
  }

  @override
  Future<void> closeConnection(double delaySeconds) async {
    if (delaySeconds == 0.0) {
      _binding.close();
    } else {
      Timer(durationFromSeconds(delaySeconds), _binding.close);
    }
  }

  @override
  Future<void> closeConnectionWithEpitaph(
      int status, double delaySeconds) async {
    if (delaySeconds == 0.0) {
      _binding.close(status);
    } else {
      Timer(durationFromSeconds(delaySeconds), () => _binding.close(status));
    }
  }

  @override
  Stream<void> get neverEvent => null;
}

StartupContext _context;
TestServerImpl _server;
TestServerBinding _binding;

void main(List<String> args) {
  _context = StartupContext.fromStartupInfo();

  _server = TestServerImpl();
  _binding = TestServerBinding();

  _context.outgoing.addPublicService<TestServer>(
      (request) => _binding.bind(_server, request), TestServer.$serviceName);
}
