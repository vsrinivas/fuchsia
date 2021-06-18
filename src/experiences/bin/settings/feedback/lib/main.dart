// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_webview_flutter/webview.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'src/feedback.dart' as feedback;

/// Main entry point to the feedback settings module
void main() async {
  setupLogger(name: 'feedback_settings');

  String url = 'https://fuchsia.dev/fuchsia-src/contribute/report-issue';

  // Sets the default web view as [FuchsiaWebView].
  WebView.platform = FuchsiaWebView.create();

  runApp(feedback.Feedback(url));
}
