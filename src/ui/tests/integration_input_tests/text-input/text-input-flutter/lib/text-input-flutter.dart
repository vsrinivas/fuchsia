// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// This is an instrumented test Flutter application. It has a single field, is
// able to receive keyboard input from the test fixture, and is able to report
// back the contents of its text field to the test fixture.

import 'package:flutter/material.dart';
import 'package:fidl_test_text/fidl_async.dart' as test_text;
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_logger/logger.dart';

void main() {
  setupLogger(name: 'text-input-flutter');

  log.info('main() started.');
  return runApp(MyApp());
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Flutter App',
        theme: ThemeData(
          primarySwatch: Colors.blue,
        ),
        home: MyHomePage(),
      );
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({Key key}) : super(key: key);

  @override
  _MyHomePageState createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  final _responseListener = test_text.ResponseListenerProxy();

  final _controller = TextEditingController();

  _MyHomePageState() {
    // Connects to a test-only API allowing this Flutter app to report the state
    // of its (only) text widget back to the test fixture.  The test fixture
    // implements the server side of this call, and will block to wait for
    // any updates from here.
    Incoming.fromSvcPath()
      ..connectToService(_responseListener)
      ..close();
  }

  @override
  void initState() {
    super.initState();
    // Configures the controller to report any changes in the text field state
    // back to the test fixture.
    _controller.addListener(() {
      final String text = _controller.text;
      _respond(test_text.Response(
        text: text,
      ));
    });
  }

  void _respond(test_text.Response response) async {
    log.info('responding with: "${response.text}"');
    await _responseListener.respond(response);
  }

  // Builds a very simple Flutter widget with a single text field that
  // is automatically in focus when the app starts.
  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(),
        body: Center(
            child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: <Widget>[
              Spacer(),
              TextField(
                key: ValueKey('textfield'),
                autofocus: true,
                controller: _controller,
                keyboardType: TextInputType.multiline,
                maxLines: 4,
                decoration: InputDecoration(
                  border: OutlineInputBorder(),
                ),
              ),
              Spacer(),
            ])));
  }
}
