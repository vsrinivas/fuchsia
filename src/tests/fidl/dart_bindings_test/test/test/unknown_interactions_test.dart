// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: avoid_catches_without_on_clauses

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_test_unknown_interactions/fidl_async.dart';
import 'package:fidl_test_unknown_interactions/fidl_test.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

/// Matches on the TXID of message.
class HasTxid extends CustomMatcher {
  HasTxid(matcher) : super('Message with txid that is', 'txid', matcher);

  @override
  int featureValueOf(dynamic actual) =>
      (actual as Uint8List).buffer.asByteData().getUint32(0, Endian.little);
}

/// Matches on the rest of a message, excluding TXID.
class ExcludingTxid extends CustomMatcher {
  ExcludingTxid(matcher)
      : super('Message, excluding txid, that is', 'message', matcher);

  @override
  List<int> featureValueOf(dynamic actual) => (actual as List<int>).sublist(4);
}

/// Matches an UnknownEvent's ordinal.
class UnknownEventWithOrdinal extends CustomMatcher {
  UnknownEventWithOrdinal(matcher)
      : super('UnknownEvent with ordinal that is', 'ordinal', matcher);

  @override
  int featureValueOf(dynamic actual) => (actual as UnknownEvent).ordinal;
}

/// Combines the TXID of an original message with a message body (excluding
/// txid) for use replying to a message.
ByteData buildReplyWithTxid(Uint8List original, List<int> body) =>
    Uint8List.fromList(original.sublist(0, 4) + body).buffer.asByteData();

/// Convert a list of ints to ByteData.
ByteData toByteData(List<int> body) =>
    Uint8List.fromList(body).buffer.asByteData();

class TestProtocol extends UnknownInteractionsProtocol$TestBase {
  TestProtocol({
    Future<void> Function() strictOneWay,
    Future<void> Function() flexibleOneWay,
    Future<void> Function() strictTwoWay,
    Future<void> Function() strictTwoWayErr,
    Future<void> Function() flexibleTwoWay,
    Future<void> Function() flexibleTwoWayErr,
    Future<void> Function(int) unknownOneWay,
    Future<void> Function(int) unknownTwoWay,
    Stream<void> strictEvent,
    Stream<void> strictEventErr,
    Stream<void> flexibleEvent,
    Stream<void> flexibleEventErr,
  })  : _strictOneWay = strictOneWay,
        _flexibleOneWay = flexibleOneWay,
        _strictTwoWay = strictTwoWay,
        _strictTwoWayErr = strictTwoWayErr,
        _flexibleTwoWay = flexibleTwoWay,
        _flexibleTwoWayErr = flexibleTwoWayErr,
        _$unknownOneWay = unknownOneWay,
        _$unknownTwoWay = unknownTwoWay,
        _strictEvent = strictEvent ?? StreamController<void>.broadcast().stream,
        _strictEventErr =
            strictEventErr ?? StreamController<void>.broadcast().stream,
        _flexibleEvent =
            flexibleEvent ?? StreamController<void>.broadcast().stream,
        _flexibleEventErr =
            flexibleEventErr ?? StreamController<void>.broadcast().stream;

  final Future<void> Function() _strictOneWay;
  final Future<void> Function() _flexibleOneWay;
  final Future<void> Function() _strictTwoWay;
  final Future<void> Function() _strictTwoWayErr;
  final Future<void> Function() _flexibleTwoWay;
  final Future<void> Function() _flexibleTwoWayErr;
  final Future<void> Function(int) _$unknownOneWay;
  final Future<void> Function(int) _$unknownTwoWay;
  final Stream<void> _strictEvent;
  final Stream<int> _strictEventFields =
      StreamController<int>.broadcast().stream;
  final Stream<void> _strictEventErr;
  final Stream<int> _strictEventFieldsErr =
      StreamController<int>.broadcast().stream;
  final Stream<void> _flexibleEvent;
  final Stream<int> _flexibleEventFields =
      StreamController<int>.broadcast().stream;
  final Stream<void> _flexibleEventErr;
  final Stream<int> _flexibleEventFieldsErr =
      StreamController<int>.broadcast().stream;

  @override
  Future<void> strictOneWay() => (_strictOneWay ?? super.strictOneWay)();
  @override
  Future<void> flexibleOneWay() => (_flexibleOneWay ?? super.flexibleOneWay)();
  @override
  Future<void> strictTwoWay() => (_strictTwoWay ?? super.strictTwoWay)();
  @override
  Future<void> strictTwoWayErr() =>
      (_strictTwoWayErr ?? super.strictTwoWayErr)();
  @override
  Future<void> flexibleTwoWay() => (_flexibleTwoWay ?? super.flexibleTwoWay)();
  @override
  Future<void> flexibleTwoWayErr() =>
      (_flexibleTwoWayErr ?? super.flexibleTwoWayErr)();
  @override
  Future<void> $unknownOneWay(int ordinal) =>
      (_$unknownOneWay ?? super.$unknownOneWay)(ordinal);
  @override
  Future<void> $unknownTwoWay(int ordinal) =>
      (_$unknownTwoWay ?? super.$unknownTwoWay)(ordinal);
  @override
  Stream<void> get strictEvent => _strictEvent;
  @override
  Stream<int> get strictEventFields => _strictEventFields;
  @override
  Stream<void> get strictEventErr => _strictEventErr;
  @override
  Stream<int> get strictEventFieldsErr => _strictEventFieldsErr;
  @override
  Stream<void> get flexibleEvent => _flexibleEvent;
  @override
  Stream<int> get flexibleEventFields => _flexibleEventFields;
  @override
  Stream<void> get flexibleEventErr => _flexibleEventErr;
  @override
  Stream<int> get flexibleEventFieldsErr => _flexibleEventFieldsErr;
}

class TestAjarProtocol extends UnknownInteractionsAjarProtocol$TestBase {
  TestAjarProtocol({
    Future<void> Function(int) unknownOneWay,
  }) : _$unknownOneWay = unknownOneWay;

  final Future<void> Function(int) _$unknownOneWay;

  @override
  Future<void> $unknownOneWay(int ordinal) =>
      (_$unknownOneWay ?? super.$unknownOneWay)(ordinal);
}

void main() {
  print('unknown-interactions-test');
  group('unknown-interactions', () {
    group('client', () {
      group('one-way-calls', () {
        UnknownInteractionsProtocolProxy proxy;
        Channel server;
        setUp(() {
          proxy = UnknownInteractionsProtocolProxy();
          server = proxy.ctrl.request().passChannel();
        });

        test('strict', () async {
          await proxy.strictOneWay();
          final ReadResult result = server.queryAndRead();
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f, //
              ]));
        });
        test('flexible', () async {
          await proxy.flexibleOneWay();
          final ReadResult result = server.queryAndRead();
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27, //
              ]));
        });
      });

      group('two-way-calls', () {
        UnknownInteractionsProtocolProxy proxy;
        ChannelReader serverEnd;
        Future<ReadResult> serverRead;
        setUp(() {
          proxy = UnknownInteractionsProtocolProxy();
          final Completer<ReadResult> serverReadCompleter = Completer();
          serverEnd = ChannelReader()
            ..onReadable = () {
              try {
                serverReadCompleter.complete(serverEnd.channel.queryAndRead());
              } catch (e, st) {
                serverReadCompleter.completeError(e, st);
              } finally {
                serverEnd
                  ..onReadable = null
                  ..onError = null;
              }
            }
            ..onError = (e) {
              serverReadCompleter.completeError(e);
              serverEnd
                ..onReadable = null
                ..onError = null;
            }
            ..bind(proxy.ctrl.request().passChannel());
          serverRead = serverReadCompleter.future;
        });

        test('strict', () async {
          final clientCall = proxy.strictTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
          ]));
          await clientCall;
        });
        test('strict-err success', () async {
          final clientCall = proxy.strictTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('strict-err transport-err', () async {
          final clientCall = proxy.strictTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with transport_err ordinal
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlStrictUnionUnknownField))));
        });
        test('flexible success', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('flexible unknown-method', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(clientCall, throwsA(isA<UnknownMethodException>()));
        });
        test('flexible other-transport-err', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlInvalidEnumValue))));
        });
        test('flexible err-variant', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlStrictUnionUnknownField))));
        });
        test('flexible-err success', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('flexible-err unknown-method', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(clientCall, throwsA(isA<UnknownMethodException>()));
        });
        test('flexible-err other-transport-err', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlInvalidEnumValue))));
        });
        test('flexible-err error-variant', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<MethodException<int>>()
                  .having((e) => e.value, 'err', equals(256))));
        });
      });

      group('events', () {
        group('open-protocol', () {
          UnknownInteractionsProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            expect(proxy.$unknownEvents.isEmpty, completion(isTrue));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.$unknownEvents,
                emits(UnknownEventWithOrdinal(equals(0xff10ff10ff10ff10))));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('ajar-protocol', () {
          UnknownInteractionsAjarProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsAjarProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            expect(proxy.$unknownEvents.isEmpty, completion(isTrue));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.$unknownEvents,
                emits(UnknownEventWithOrdinal(equals(0xff10ff10ff10ff10))));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          UnknownInteractionsClosedProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsClosedProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
    });

    group('server', () {
      Future<ReadResult> read(Channel channel) {
        final Completer<ReadResult> readCompleter = Completer();
        ChannelReader reader = ChannelReader();
        reader
          ..onReadable = () {
            try {
              readCompleter.complete(reader.channel.queryAndRead());
            } catch (e, st) {
              readCompleter.completeError(e, st);
            } finally {
              reader
                ..onReadable = null
                ..onError = null;
            }
          }
          ..onError = (e) {
            readCompleter.completeError(e);
            reader
              ..onReadable = null
              ..onError = null;
          }
          ..bind(channel);
        return readCompleter.future;
      }

      group('one-way-calls', () {
        group('open-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsProtocolBinding binding =
                UnknownInteractionsProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(unknownOneWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('ajar-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final Channel client = UnknownInteractionsAjarProtocolBinding()
                .wrap(server)
                .passChannel();
            expect(unknownOneWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          test('unknown-strict', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
      group('two-way-calls', () {
        group('open-protocol', () {
          test('known-strict', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(strictTwoWay: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                  0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
                ]));
          });
          test('known-strict-err', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(strictTwoWayErr: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                  0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(flexibleTwoWay: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible-err success', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server =
                TestProtocol(flexibleTwoWayErr: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible-err error', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server =
                TestProtocol(flexibleTwoWayErr: () async {
              callReceived.complete();
              throw MethodException(103);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
                  // Result union with err:
                  // ordinal  ---------------------------------|
                  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('unknown-strict', () {
            final Completer<int> unknownTwoWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownTwoWay: (int ordinal) async {
              unknownTwoWayCall.complete(ordinal);
            });
            final UnknownInteractionsProtocolBinding binding =
                UnknownInteractionsProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownTwoWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final Completer<int> unknownTwoWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownTwoWay: (int ordinal) async {
              unknownTwoWayCall.complete(ordinal);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(unknownTwoWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
                  // Result union with transport_err:
                  // ordinal  ---------------------------------|
                  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
        });
        group('ajar-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          test('unknown-strict', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
      group('events', () {
        test('strict', () async {
          final TestProtocol server =
              TestProtocol(strictEvent: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x38, 0x27, 0xa3, 0x91, 0x98, 0x41, 0x4b, 0x58, //
              ]));
        });
        test('strict-err', () async {
          final TestProtocol server =
              TestProtocol(strictEventErr: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0xe8, 0xc1, 0x96, 0x8e, 0x1e, 0x34, 0x7c, 0x53, //
                // Result union with success:
                // ordinal  ---------------------------------|
                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
        test('flexible', () async {
          final TestProtocol server =
              TestProtocol(flexibleEvent: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x6c, 0x2c, 0x80, 0x0b, 0x8e, 0x1a, 0x7a, 0x31, //
              ]));
        });
        test('flexible-err success', () async {
          final TestProtocol server =
              TestProtocol(flexibleEventErr: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xca, 0xa7, 0x49, 0xfa, 0x0e, 0x90, 0xe7, 0x41, //
                // Result union with success:
                // ordinal  ---------------------------------|
                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
        test('flexible-err error', () async {
          final TestProtocol server = TestProtocol(
              flexibleEventErr:
                  Stream.fromFuture(Future.error(MethodException(256))));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xca, 0xa7, 0x49, 0xfa, 0x0e, 0x90, 0xe7, 0x41, //
                // Result union with error:
                // ordinal  ---------------------------------|
                0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
      });
    });
  });
}
