// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';

class Feedback extends StatelessWidget {
  final String url;

  const Feedback(this.url);

  @override
  Widget build(BuildContext context) => MaterialApp(home: FeedbackPage(url));
}

class FeedbackPage extends StatefulWidget {
  final String url;

  const FeedbackPage(this.url);

  @override
  _FeedbackPageState createState() => _FeedbackPageState();
}

class _FeedbackPageState extends State<FeedbackPage> {
  @override
  Widget build(BuildContext context) => Scaffold(
        // TODO(fxb/78917): Fix text input.
        body: WebView(
          initialUrl: widget.url,
        ),
      );
}
