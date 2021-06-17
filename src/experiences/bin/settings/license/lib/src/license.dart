// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';

class License extends StatelessWidget {
  final String url;

  const License(this.url);

  @override
  Widget build(BuildContext context) => MaterialApp(home: LicensePage(url));
}

class LicensePage extends StatefulWidget {
  final String url;

  const LicensePage(this.url);

  @override
  _LicensePageState createState() => _LicensePageState();
}

class _LicensePageState extends State<LicensePage> {
  @override
  Widget build(BuildContext context) => Scaffold(
        body: WebView(
          initialUrl: widget.url,
        ),
      );
}
