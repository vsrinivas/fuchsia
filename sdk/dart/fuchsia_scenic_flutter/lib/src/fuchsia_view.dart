// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';

import 'fuchsia_view_controller.dart';

/// A widget that is replaced by content from another process.
class FuchsiaView extends StatelessWidget {
  /// The [PlatformViewController] used to control this [FuchsiaView].
  final FuchsiaViewController controller;

  /// Whether this child should be included during hit testing.
  ///
  /// Defaults to true.
  final bool hitTestable;

  /// Whether this child and its children should be allowed to receive focus.
  ///
  /// Defaults to true.
  final bool focusable;

  /// Creates a widget that is replaced by content from another process.
  FuchsiaView({
    required this.controller,
    this.hitTestable = true,
    this.focusable = true,
  }) : super(key: GlobalObjectKey(controller));

  @override
  Widget build(BuildContext context) {
    return PlatformViewLink(
      viewType: 'fuchsiaView',
      onCreatePlatformView: (params) => controller
        ..connect(hitTestable: hitTestable, focusable: focusable).then((_) {
          params.onPlatformViewCreated(controller.viewId);
        }),
      surfaceFactory: (context, controller) {
        return PlatformViewSurface(
          gestureRecognizers: const <Factory<OneSequenceGestureRecognizer>>{},
          controller: controller,
          hitTestBehavior: hitTestable
              ? PlatformViewHitTestBehavior.opaque
              : PlatformViewHitTestBehavior.transparent,
        );
      },
    );
  }
}
