// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

/// Uses [ui.window] to create a [MediaQuery] parent for [child].
class WindowMediaQuery extends StatefulWidget {
  /// Called when the window metrics change.
  final VoidCallback onWindowMetricsChanged;

  /// The [Widget] to be given a [MediaQuery] parent.
  final Widget child;

  /// Constructor.
  const WindowMediaQuery({this.onWindowMetricsChanged, this.child});

  @override
  _WindowMediaQueryState createState() => _WindowMediaQueryState();
}

class _WindowMediaQueryState extends State<WindowMediaQuery>
    with WidgetsBindingObserver {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MediaQuery(
        data: MediaQueryData.fromWindow(ui.window),
        child: widget.child,
      );

  @override
  void didChangeMetrics() => setState(() {
        widget.onWindowMetricsChanged?.call();
      });
}
