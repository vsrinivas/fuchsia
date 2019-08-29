// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show ascii;
import 'dart:developer' show Timeline;
import 'dart:ui' show window;

import 'package:fidl_fuchsia_ui_input/fidl_async.dart'
    show KeyboardEvent, kModifierLeftAlt, KeyboardEventPhase;
import 'package:flutter/widgets.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';
import 'package:lib.widgets/widgets.dart';
import 'package:lib.story_shell/common.dart';
import 'models/inset_manager.dart';
import 'models/layout_model.dart';
import 'models/surface/surface_graph.dart';
import 'story_shell_impl.dart';
import 'widgets/mondrian.dart';
import 'widgets/surface_relationships.dart';

/// This is used for keeping the reference around.
// ignore: unused_element
StoryShellImpl _storyShellImpl;

/// Entry point.
void main() {
  setupLogger(name: 'Mondrian');
  Timeline.instantSync('starting');
  log.info('Started');

  LayoutModel layoutModel = LayoutModel();
  final ValueNotifier overviewNotifier = ValueNotifier(false);
  InsetManager insetManager = InsetManager();
  KeyListener keyListener = KeyListener()
    ..registerKeyboardEventCallback(
      event: KeyboardEvent(
          deviceId: 0,
          eventTime: 0,
          hidUsage: 0,
          codePoint: ascii.encode('o')[0],
          modifiers: kModifierLeftAlt,
          phase: KeyboardEventPhase.pressed),
      callback: () => overviewNotifier.value = !overviewNotifier.value,
    );

  final surfaceGraph = SurfaceGraph();
  surfaceGraph.addListener(() {
    insetManager.onSurfacesChanged(surfaces: surfaceGraph.size);
  });

  void onWindowMetricsChanged() {
    Timeline.instantSync(
      'Mondrian: onWindowMetricsChanged '
          '${MediaQueryData.fromWindow(window).size.width},'
          '${MediaQueryData.fromWindow(window).size.height}',
    );
  }

  // Note: This implementation only supports one StoryShell at a time.
  // Initialize the one Flutter application we support
  runApp(
    Directionality(
      textDirection: TextDirection.ltr,
      child: WindowMediaQuery(
        onWindowMetricsChanged: onWindowMetricsChanged,
        child: ScopedModel<LayoutModel>(
          model: layoutModel,
          child: ScopedModel<InsetManager>(
            model: insetManager,
            child: ScopedModel<SurfaceGraph>(
              model: surfaceGraph,
              child: AnimatedBuilder(
                animation: overviewNotifier,
                builder: (BuildContext context, Widget child) =>
                    overviewNotifier.value
                        ? SurfaceRelationships()
                        : Mondrian(),
              ),
            ),
          ),
        ),
      ),
    ),
  );

  _storyShellImpl =
      StoryShellImpl(surfaceGraph: surfaceGraph, keyListener: keyListener)
        ..advertise();
  Timeline.instantSync('started');
}
