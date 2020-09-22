// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:collection/collection.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Scenic scenicDriver;
  HttpServer server;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    scenicDriver = sl4f.Scenic(sl4fDriver);

    final serverIp = Platform.environment['SERVER_IP'];
    if (serverIp == null) {
      fail('SERVER_IP environment variable not set.');
    }

    // TODO(fxbug.dev/10031): Figure out how to get data into CI to actually test.  For
    //                now, we'll just leave that to the user.
    final fileName = Platform.environment['MEDIA_FILE'];
    if (fileName == null) {
      fail('MEDIA_FILE environment variable not set.');
    }

    final file = File(fileName);
    final filesize = await file.length();

    server = await HttpServer.bind(serverIp, 8000)
      ..listen((HttpRequest request) {
        request.response.statusCode = HttpStatus.ok;
        request.response.headers.contentType = ContentType.parse('video/mp4');

        request.response.contentLength = filesize;

        Future f = file.readAsBytes();

        request.response.addStream(f.asStream()).whenComplete(() {
          request.response.close();
        });
      });
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
    if (server != null) {
      await server.close(force: true);
    }
  });

  test('play video', () async {
    final before = await scenicDriver.takeScreenshot(dumpName: 'before');
    await scenicDriver.takeScreenshot(dumpName: 'before');

    var cmd = 'sessionctl add_mod http://${server.address.address}:8000/';
    await sl4fDriver.ssh.run(cmd);
    await Future.delayed(
        Duration(seconds: 5)); // Wait for video to start playing

    final after = await scenicDriver.takeScreenshot(dumpName: 'after');
    await Future.delayed(Duration(seconds: 5));
    final after2 = await scenicDriver.takeScreenshot(dumpName: 'after2');

    if (DeepCollectionEquality().equals(before.data, after.data)) {
      fail('Playing video did not cause the screen to change');
    }

    if (DeepCollectionEquality().equals(after.data, after2.data)) {
      fail('Got identical frames 5s apart in the video');
    }
    // TODO: How do we dismiss the mod we added above?
  },
      // This is a large test that waits for the DUT to come up and to start
      // rendering something.
      timeout: Timeout.none);
}
