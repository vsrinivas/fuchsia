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

    test('args does not include key path when using agent', () async {
      final ssh = Ssh.useAgent('1.2.3.4');

      final args = ssh.makeArgs('foo');
      expect(args, containsAllInOrder(['fuchsia@1.2.3.4', 'foo']));
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
