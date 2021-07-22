// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:ffx/ffx.dart' show Ffx, FfxRunner, FfxException;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

final Matcher throwsFfxException = throwsA(isA<FfxException>());

void main() async {
  group('Ffx.fromEnvironment', () {
    late MockFfxRunner mockRunner;

    setUp(() async {
      final targetsJson = json.encode([
        {
          'nodename': 'fuchsia-265e-227f-af0e',
          'rcs_state': 'N',
          'serial': '<unknown>',
          'target_type': 'Unknown',
          'target_state': 'Product',
          'addresses': ['192.168.42.89', 'fe80::cc31:32ba:1ece:e338%br0']
        },
        {
          'nodename': '<unknown>',
          'rcs_state': 'N',
          'serial': '<unknown>',
          'target_type': 'Unknown',
          'target_state': 'Product',
          'addresses': ['::1']
        },
        {
          'nodename': 'fuchsia-43c7-7d5b-6a21',
          'rcs_state': 'N',
          'serial': '<unknown>',
          'target_type': 'Unknown',
          'target_state': 'Product',
          'addresses': ['192.168.42.21', 'fe80::dd1c:2480:103c:276c%br0']
        }
      ]);
      mockRunner = MockFfxRunner();
      when(mockRunner.runWithOutput(['target', 'list', '--format', 'json']))
          .thenAnswer((_) => Future.value(Stream.value(targetsJson)));
    });

    test('configures Ffx with a nodename from FUCHSIA_NODENAME', () async {
      const nodename = 'test-nodename';
      final environment = {'FUCHSIA_NODENAME': nodename};
      final ffx = await Ffx.fromEnvironment(environment: environment);
      expect(ffx.nodename, nodename);
    });

    test('configures Ffx with a nodename from FUCHSIA_DEVICE_ADDR', () async {
      final environment = {
        'FUCHSIA_DEVICE_ADDR': 'fe80::cc31:32ba:1ece:e338%br0'
      };
      final ffx = await Ffx.fromEnvironment(
          environment: environment, runner: mockRunner);
      expect(ffx.nodename, 'fuchsia-265e-227f-af0e');
    });

    test('configures Ffx with a nodename from FUCHSIA_IPV4_ADDR', () async {
      final environment = {'FUCHSIA_IPV4_ADDR': '192.168.42.21'};
      final ffx = await Ffx.fromEnvironment(
          environment: environment, runner: mockRunner);
      expect(ffx.nodename, 'fuchsia-43c7-7d5b-6a21');
    });

    test('configures Ffx with a nodename from FUCHSIA_IPV6_ADDR', () async {
      final environment = {
        'FUCHSIA_IPV6_ADDR': 'fe80::dd1c:2480:103c:276c%br0'
      };
      final ffx = await Ffx.fromEnvironment(
          environment: environment, runner: mockRunner);
      expect(ffx.nodename, 'fuchsia-43c7-7d5b-6a21');
    });
  });

  group('Ffx.fromAddress', () {
    late MockFfxRunner mockRunner;

    setUp(() async {
      mockRunner = MockFfxRunner();
    });

    group('with an empty target list', () {
      setUp(() async {
        final targetsJson = json.encode([]);
        when(mockRunner.runWithOutput(['target', 'list', '--format', 'json']))
            .thenAnswer((_) => Future.value(Stream.value(targetsJson)));
      });

      test('throws', () async {
        expect(
            Ffx.fromAddress('1.2.3.4', runner: mockRunner), throwsFfxException);
      });
    });

    group('with a target list', () {
      setUp(() async {
        final targetsJson = json.encode([
          {
            'nodename': 'fuchsia-265e-227f-af0e',
            'rcs_state': 'N',
            'serial': '<unknown>',
            'target_type': 'Unknown',
            'target_state': 'Product',
            'addresses': ['192.168.42.89', 'fe80::cc31:32ba:1ece:e338%br0']
          }
        ]);
        when(mockRunner.runWithOutput(['target', 'list', '--format', 'json']))
            .thenAnswer((_) => Future.value(Stream.value(targetsJson)));
      });

      test('configures Ffx with a nodename from the target list', () async {
        final ffx = await Ffx.fromAddress('192.168.42.89', runner: mockRunner);
        expect(ffx.nodename, 'fuchsia-265e-227f-af0e');
      });

      test('throws if the target list does not have a matching target',
          () async {
        expect(
            Ffx.fromAddress('1.2.3.4', runner: mockRunner), throwsFfxException);
      });
    });
  });

  group('Ffx.run', () {
    test('invokes ffx with --target <nodename> and given args', () async {
      const nodename = 'fuchsia-265e-227f-af0e';
      final mockRunner = MockFfxRunner();
      final ffx = Ffx(nodename, mockRunner);

      final args = ['arg1', 'arg2'];
      await ffx.run(args);
      verify(mockRunner.run(['--target', nodename] + args));
    });
  });
}

class MockFfxRunner extends Mock implements FfxRunner {}
