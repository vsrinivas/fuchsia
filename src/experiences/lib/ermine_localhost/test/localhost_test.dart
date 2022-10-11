// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:ermine_localhost/localhost.dart';
import 'package:test/test.dart';

void main() {
  test('bindServer should start listening on 127.0.0.1:8080 by default',
      () async {
    final localhost = Localhost();
    final address = await localhost.bindServer();
    expect(address, 'http://127.0.0.1:8080');
    localhost.stopServer();
  });

  test(
      'bindServer should start listening on 127.0.0.1:<port> '
      'when the port is given', () async {
    final localhost = Localhost();
    final address = await localhost.bindServer(port: 8000);
    expect(address, 'http://127.0.0.1:8000');
    localhost.stopServer();
  });

  test('passWebFile should save files with the right key', () async {
    final localhost = Localhost();

    final htmlFile = File('pkg/testdata/test.html');
    final cssFile = File('pkg/testdata/test.css');
    final txtFile = File('pkg/testdata/test.txt');

    expect(localhost.pages.length, 0);

    localhost.passWebFile(htmlFile);
    expect(localhost.pages.length, 1);
    expect(localhost.pages['test.html'], htmlFile);

    localhost.passWebFile(cssFile);
    expect(localhost.pages.length, 2);
    expect(localhost.pages['test.css'], cssFile);

    localhost.passWebFile(txtFile);
    expect(localhost.pages.length, 3);
    expect(localhost.pages['test.txt'], txtFile);

    localhost.stopServer();
  });

  test(
      'passWebFile should ignore the new same-named file when replace is false',
      () async {
    final localhost = Localhost();
    final txtFile = File('pkg/testdata/test.txt');
    final anotherTxtFile = File('pkg/testdata/another/test.txt');

    expect(localhost.pages.length, 0);

    localhost.passWebFile(txtFile);
    expect(localhost.pages.length, 1);
    expect(localhost.pages['test.txt'], txtFile);

    localhost.passWebFile(anotherTxtFile, replace: false);
    expect(localhost.pages.length, 1);
    expect(localhost.pages['test.txt'], txtFile);

    localhost.passWebFile(anotherTxtFile);
    expect(localhost.pages.length, 1);
    expect(localhost.pages['test.txt'], anotherTxtFile);
  });
}
