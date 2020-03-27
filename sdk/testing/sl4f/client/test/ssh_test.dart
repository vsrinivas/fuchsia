// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

void main(List<String> args) {
  group(Ssh, () {
    test('args includes key path when passed', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key');

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['-i', '/path/to/p/key']));
      expect(args, containsAllInOrder(['fuchsia@1.2.3.4', 'foo']));
    });

    test('IPv6 address without ssh port', () async {
      final ssh = Ssh('::1', '/path/to/p/key');

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['-i', '/path/to/p/key']));
      expect(args, containsAllInOrder(['fuchsia@::1', 'foo']));
      expect(args, isNot(contains('-p')));
    });

    test('IPv6 address with ssh port', () async {
      final ssh = Ssh('::1', '/path/to/p/key', 8022);

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['-i', '/path/to/p/key']));
      expect(args, containsAllInOrder(['fuchsia@::1', 'foo']));
      expect(args, containsAllInOrder(['-p', '8022']));
    });

    test('IPv6 linklocal address with ssh port', () async {
      final ssh = Ssh('fe80::1234:44f%eth0', '/path/to/p/key', 8022);

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['-i', '/path/to/p/key']));
      expect(args, containsAllInOrder(['fuchsia@fe80::1234:44f%eth0', 'foo']));
      expect(args, containsAllInOrder(['-p', '8022']));
    });

    test('args includes ssh port and key path', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key', 8022);

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['-i', '/path/to/p/key']));
      expect(args, containsAllInOrder(['fuchsia@1.2.3.4', 'foo']));
      expect(args, containsAllInOrder(['-p', '8022']));
    });

    test('args does not include key path when using agent', () async {
      final ssh = Ssh.useAgent('1.2.3.4');

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['fuchsia@1.2.3.4', 'foo']));
      expect(args, isNot(contains('-i')));
    });

    test('args includes ssh port and does not include key path', () async {
      final ssh = Ssh.useAgent('1.2.3.4', 8022);

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['fuchsia@1.2.3.4', 'foo']));
      expect(args, containsAllInOrder(['-p', '8022']));
      expect(args, isNot(contains('-i')));
    });

    test('port forwarding args includes ports', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key');

      final args = ssh.makeForwardArgs(1, 2);
      expect(args, containsAllInOrder(['-L', 'localhost:1:localhost:2']));
      expect(args, containsAllInOrder(['-O', 'forward']));
    });

    test('cancelling port forwarding args includes ports', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key');

      final args = ssh.makeForwardArgs(1, 2, cancel: true);
      expect(args, containsAllInOrder(['-L', 'localhost:1:localhost:2']));
      expect(args, containsAllInOrder(['-O', 'cancel']));
    });

    test('remote port forwarding args includes ports', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key');

      final args = ssh.makeRemoteForwardArgs(1, 2);
      expect(args, containsAllInOrder(['-R', 'localhost:1:localhost:2']));
      expect(args, containsAllInOrder(['-O', 'forward']));
    });

    test('cancelling remote port forwarding args includes ports', () async {
      final ssh = Ssh('1.2.3.4', '/path/to/p/key');

      final args = ssh.makeRemoteForwardArgs(1, 2, cancel: true);
      expect(args, containsAllInOrder(['-R', 'localhost:1:localhost:2']));
      expect(args, containsAllInOrder(['-O', 'cancel']));
    });
  });
}
