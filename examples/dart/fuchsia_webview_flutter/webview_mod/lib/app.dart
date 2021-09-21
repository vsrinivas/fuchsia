// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:webview_flutter/webview_flutter.dart';

const kSampleScript = """
(function() {
  let counter = 0;
  let exampleInterval = setInterval(function() {
    if (counter > 60) {
      clearInterval(exampleInterval);
      return;
    }
    ExampleHostChannel.postMessage(`it's been \${counter} seconds since the page loaded`);
    counter += 1;
  }, 1000);
})();
""";

class App extends StatefulWidget {
  const App({
    Key key,
  }) : super(key: key);
  @override
  State<App> createState() => AppState();
}

class AppState extends State<App> {
  final TextEditingController _textEditingController;
  WebViewController _webViewController;

  AppState() : _textEditingController = TextEditingController();

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Webview Mod',
      home: Scaffold(
        backgroundColor: Colors.white,
        body: Column(
          children: <Widget>[
            Row(
              children: <Widget>[
                _buildBackBtn(),
                _buildFwdBtn(),
                _buildReloadBtn(),
                _buildUrlTextField(),
                _buildEnterBtn(),
                _buildClearBtn(),
                _buildCurrentBtn(),
              ],
            ),
            _buildWebview(),
          ],
        ),
      ),
    );
  }

  Widget _buildUrlTextField() {
    return Expanded(
      child: TextField(
        controller: _textEditingController,
        decoration: InputDecoration(
            border: OutlineInputBorder(
              borderRadius: const BorderRadius.all(
                Radius.circular(25.0),
              ),
            ),
            filled: true,
            hintStyle: TextStyle(color: Colors.grey[800]),
            hintText: 'Enter URL',
            fillColor: Colors.white70),
      ),
    );
  }

  Widget _buildEnterBtn() {
    return IconButton(
      icon: Icon(Icons.play_arrow),
      tooltip: 'Enter',
      onPressed: () async => await _loadUrl(_textEditingController.text),
    );
  }

  Widget _buildClearBtn() {
    return IconButton(
      icon: Icon(Icons.clear),
      tooltip: 'Clear',
      onPressed: _textEditingController.clear,
    );
  }

  Widget _buildCurrentBtn() {
    return ElevatedButton(
      child: const Text('Current'),
      style: ElevatedButton.styleFrom(
        primary: Theme.of(context).colorScheme.secondary,
        onPrimary: Colors.blueGrey,
        elevation: 4.0,
      ),
      onPressed: () {
        _webViewController.currentUrl().then((url) {
          _textEditingController.text = url;
        });
      },
    );
  }

  Widget _buildReloadBtn() {
    return IconButton(
      icon: Icon(Icons.refresh),
      tooltip: 'Reload',
      onPressed: () => _webViewController.reload(),
    );
  }

  Widget _buildBackBtn() {
    return IconButton(
        icon: Icon(Icons.arrow_back),
        tooltip: 'Back',
        onPressed: () => _webViewController?.goBack());
  }

  Widget _buildFwdBtn() {
    return IconButton(
        icon: Icon(Icons.arrow_forward),
        tooltip: 'Forward',
        onPressed: () => _webViewController?.goForward());
  }

  Widget _buildWebview() {
    return Expanded(
      child: Container(
        child: WebView(
          onWebViewCreated: (WebViewController controller) {
            _webViewController = controller;
          },
          initialUrl: 'https://google.com',
          debuggingEnabled: true,
          javascriptMode: JavascriptMode.unrestricted,
          onPageFinished: (url) async {
            // Injects a sample script that notifies every second through the
            // ExampleHostChannel for the first minute the page is loaded.
            await _webViewController?.evaluateJavascript(kSampleScript);
          },
          javascriptChannels: {
            JavascriptChannel(
              name: 'ExampleHostChannel',
              onMessageReceived: (message) {
                log.info('Got message from the page: ${message.message}');
              },
            ),
          },
        ),
      ),
    );
  }

  Future<void> _loadUrl(String url) async {
    if (url == null || url.isEmpty) {
      return;
    }
    Uri uri = Uri.parse(url);
    if (!uri.hasScheme) {
      uri = uri.replace(scheme: 'https');
    }
    await _webViewController?.loadUrl(uri.toString());
  }
}
