// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:quiver/testing/async.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

import 'package:sl4f/sl4f.dart';

class MockDump extends Mock implements Dump {}

class MockIOSink extends Mock implements IOSink {}

class MockSsh extends Mock implements Ssh {}

class MockProcess extends Mock implements Process {}

class MockStream<T> extends Mock implements Stream<T> {}

class MockSubscription<T> extends Mock implements StreamSubscription<T> {}

void main() {
  group('Sl4f diagnostics', () {
    test('are performed if dump is enabled', () async {
      final dump = MockDump();
      final ssh = MockSsh();
      final sl4f = Sl4f('', ssh);

      final dumps = <String, Future<String>>{};

      when(dump.hasDumpDirectory).thenReturn(true);
      when(dump.openForWrite(any, any)).thenAnswer((invocation) {
        // Just ensure that the names are unique and don't have spaces.
        final name = invocation.positionalArguments[0];
        expect(name, isNot(contains(' ')));

        // ignore: close_sinks
        final controller = StreamController<List<int>>();
        dumps[name] = systemEncoding.decodeStream(controller.stream);
        return IOSink(controller, encoding: systemEncoding);
      });
      // Use `echo -n` as our invocation so we can verify both that the expected
      // command was issued and that the dump works, as the command itself is
      // dumped.
      when(ssh.start(any)).thenAnswer((invocation) => Process.start(
          '/bin/echo', ['-n', invocation.positionalArguments[0]]));

      await sl4f.dumpDiagnostics('dump', dump: dump);
      expect(dumps, hasLength(Sl4f.diagnostics.length));
      expect(await Future.wait(dumps.values),
          unorderedEquals(Sl4f.diagnostics.values));
    });

    test('are skipped if dump is not enabled', () async {
      final dump = MockDump();
      final ssh = MockSsh();
      final sl4f = Sl4f('', ssh);

      when(dump.hasDumpDirectory).thenReturn(false);
      await sl4f.dumpDiagnostics('you cannot dump', dump: dump);
      verifyNever(ssh.start(any));
    });

    // CF-876
    test('do not hang even if the tools do', () {
      const eventually = Duration(hours: 1);

      FakeAsync().run((async) {
        final dump = MockDump();
        final ssh = MockSsh();
        // ignore: close_sinks
        final mockIOSink = MockIOSink();
        final sl4f = Sl4f('', ssh);
        final verifiers = [];

        when(dump.hasDumpDirectory).thenReturn(true);
        when(dump.openForWrite(any, any)).thenAnswer((_) => mockIOSink);
        when(mockIOSink.add(any)).thenReturn(null);
        when(ssh.start(any)).thenAnswer((_) async {
          // Using an actual process or streams here causes problems when the
          // code under test attempts to cancel the stream subscriptions, as
          // the resulting future does not complete immediately. So we create
          // a mock process where stdout and stderr are mock streams that give
          // mock subscriptions that verify that they are cancelled.
          final mockProcess = MockProcess();
          when(mockProcess.kill(any)).thenAnswer((_) => null);
          when(mockProcess.stdin).thenAnswer((_) => MockIOSink());
          // These mock streams just provide a StreamSubscription that can be
          // cancelled, but never produce any values.
          when(mockProcess.stdout).thenAnswer((_) {
            final mockStream = MockStream<List<int>>();
            when(mockStream.listen(any, onDone: anyNamed('onDone')))
                .thenAnswer((_) {
              final mockSubscription = MockSubscription<List<int>>();
              when(mockSubscription.cancel())
                  .thenAnswer((_) => Future.value(null));
              verifiers.add(() => verify(mockSubscription.cancel()).called(1));
              return mockSubscription;
            });
            return mockStream;
          });
          when(mockProcess.stderr).thenAnswer((_) {
            final mockStream = MockStream<List<int>>();
            when(mockStream.listen(any, onDone: anyNamed('onDone')))
                .thenAnswer((_) {
              final mockSubscription = MockSubscription<List<int>>();
              when(mockSubscription.cancel())
                  .thenAnswer((_) => Future.value(null));
              verifiers.add(() => verify(mockSubscription.cancel()).called(1));
              return mockSubscription;
            });
            return mockStream;
          });

          return mockProcess;
        });

        expect(sl4f.dumpDiagnostics('dumpName', dump: dump).timeout(eventually),
            completes);
        async.elapse(eventually);

        for (final verifier in verifiers) {
          verifier();
        }
      });
    });

    test('hostIpAddress', () async {
      final ssh = MockSsh();
      final sl4f = Sl4f('', ssh);
      // Normally, both addresses (before and after the ~) will be the same.
      // They differ in this test for the sake of testing (knowing which is
      // used).
      when(ssh.runWithOutput(any)).thenAnswer((invocation) => Process.run(
          '/bin/echo',
          ['fe80::fefe:fefe:fefe:fefe%4 234~fe80::fefe:fefe:fefe:abab%4 987']));
      expect(await sl4f.hostIpAddress(), equals('fe80::fefe:fefe:fefe:fefe%4'));
      when(ssh.runWithOutput(any)).thenAnswer((invocation) =>
          Process.run('/bin/echo', ['-n', '~fe80::fefe:fefe:fefe:abab%4 987']));
      expect(await sl4f.hostIpAddress(), equals('fe80::fefe:fefe:fefe:abab%4'));
      when(ssh.runWithOutput(any))
          .thenAnswer((invocation) => Process.run('/bin/echo', ['-n', '~']));
      expect(() async => await sl4f.hostIpAddress(),
          throwsA(TypeMatcher<Sl4fException>()));
    });
  });

  group('Sl4f fromEnvironment', () {
    test('throws exception if no IP address', () {
      expect(() => Sl4f.fromEnvironment(environment: {}),
          throwsA(TypeMatcher<Sl4fException>()));
    });
    test('throws exception if ssh cannot be used', () {
      expect(
          () => Sl4f.fromEnvironment(
              environment: {'FUCHSIA_IPV4_ADDR': '1.2.3.4'}),
          throwsA(TypeMatcher<Sl4fException>()));
    });

    test('accepts SSH_AUTH_SOCK', () {
      expect(
          Sl4f.fromEnvironment(environment: {
            'FUCHSIA_IPV4_ADDR': '1.2.3.4',
            'SSH_AUTH_SOCK': '/foo'
          }),
          TypeMatcher<Sl4f>());
    });

    test('accepts FUCHSIA_SSH_KEY', () {
      expect(
          Sl4f.fromEnvironment(environment: {
            'FUCHSIA_IPV4_ADDR': '1.2.3.4',
            'FUCHSIA_SSH_KEY': '/foo'
          }),
          TypeMatcher<Sl4f>());
    });

    test('accepts FUCHSIA_SSH_PORT', () {
      expect(
          Sl4f.fromEnvironment(environment: {
            'FUCHSIA_IPV4_ADDR': '1.2.3.4',
            'FUCHSIA_SSH_KEY': '/foo',
            'FUCHSIA_SSH_PORT': '8022'
          }),
          TypeMatcher<Sl4f>());
    });

    test('default HTTP Port', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV4_ADDR': '1.2.3.4',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022'
      });
      expect(client.port, equals(80));
    });

    test('override HTTP Port', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV4_ADDR': '1.2.3.4',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.port, equals(8282));
    });

    test('IPv6 address using FUCHSIA_IPV4_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV4_ADDR': '::1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.target, equals('[::1]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 address using FUCHSIA_DEVICE_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_DEVICE_ADDR': '::1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.target, equals('[::1]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 link-local address using FUCHSIA_DEVICE_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_DEVICE_ADDR': 'fe80::1234:44f%eth0',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.target, equals('[fe80::1234:44f%eth0]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 address using FUCHSIA_DEVICE_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV6_ADDR': '::1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.target, equals('[::1]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 link-local address using FUCHSIA_DEVICE_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV6_ADDR': 'fe80::1234:44f%eth0',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      expect(client.target, equals('[fe80::1234:44f%eth0]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 address using both FUCHSIA_IPV4_ADDR and FUCHSIA_DEVICE_ADDR',
        () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_DEVICE_ADDR': '::1',
        'FUCHSIA_IPV4_ADDR': '127.0.0.1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      // FUCHSIA_DEVICE_ADDR has preference over FUCHSIA_IPV4_ADDR:
      expect(client.target, equals('[::1]'));
      expect(client.port, equals(8282));
    });

    test('IPv6 address using both FUCHSIA_IPV4_ADDR and FUCHSIA_IPV6_ADDR', () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_IPV6_ADDR': '::1',
        'FUCHSIA_IPV4_ADDR': '127.0.0.1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      // FUCHSIA_IPV4_ADDR has preference over FUCHSIA_IPV6_ADDR:
      expect(client.target, equals('127.0.0.1'));
      expect(client.port, equals(8282));
    });

    test(
        'IPv6 address using FUCHSIA_IPV4_ADDR and FUCHSIA_IPV6_ADDR and FUCHSIA_DEVICE_ADDR',
        () {
      Sl4f client = Sl4f.fromEnvironment(environment: {
        'FUCHSIA_DEVICE_ADDR': '::1',
        'FUCHSIA_IPV6_ADDR': 'fe80::1234:44f%eth0',
        'FUCHSIA_IPV4_ADDR': '127.0.0.1',
        'FUCHSIA_SSH_KEY': '/foo',
        'FUCHSIA_SSH_PORT': '8022',
        'SL4F_HTTP_PORT': '8282'
      });
      // FUCHSIA_DEVICE_ADDR has preference over the other two:
      expect(client.target, equals('[::1]'));
      expect(client.port, equals(8282));
    });
  });

  group('Sl4f constructor', () {
    test('empty path in targetUrl', () {
      Sl4f client = Sl4f('1.2.3.4', null);
      expect(client.targetUrl.path, isEmpty);
    });
    test('throws exception if target ipv4 address has port', () {
      expect(() => Sl4f('1.2.3.4:8282', null),
          throwsA(TypeMatcher<FormatException>()));
    });
    test('throws exception if target ipv6 address has port', () {
      expect(() => Sl4f('[::1]:8282', null),
          throwsA(TypeMatcher<FormatException>()));
    });
  });

  group('Sl4f fromUrl', () {
    test('correct target and port', () {
      Sl4f client = Sl4f.fromUrl(Uri.http('sl4f.com:1234', ''));
      expect(client.target, equals('sl4f.com'));
      expect(client.port, equals(1234));
    });
    test('null url throws exception', () {
      expect(() => Sl4f.fromUrl(null), throwsA(TypeMatcher<ArgumentError>()));
    });
    test('header is set', () {
      Sl4f client =
          Sl4f.fromUrl(Uri.http('sl4f.com:1234', ''), {'testKey': 'testValue'});
      expect(client.headers, contains('testKey'));
      expect(client.headers['testKey'], equals('testValue'));
    });
  });
}
