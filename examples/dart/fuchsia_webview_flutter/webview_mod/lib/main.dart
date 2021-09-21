// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/module.dart' as modular;
import 'package:fuchsia_webview_flutter/webview.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'app.dart';

void main() {
  WebView.platform = FuchsiaWebView.create();
  setupLogger(name: 'Webview Mod');
  modular.Module();
  runApp(App());
}
