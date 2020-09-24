// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines a class to receive a callback when the supplied [child] widget
/// is rendered.
///
/// The [onRender] callback is invoked on subsequent frame after every [build].
class PostRender extends StatelessWidget {
  final Widget child;
  final VoidCallback onRender;

  const PostRender({this.child, this.onRender});

  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance.addPostFrameCallback((_) => onRender?.call());
    return child;
  }
}
