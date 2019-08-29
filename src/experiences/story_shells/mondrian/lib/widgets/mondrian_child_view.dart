// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:fuchsia_logger/logger.dart';
import '../models/surface/surface.dart';

import 'breathing_placeholder.dart';

/// Frame for child views
class MondrianChildView extends StatelessWidget {
  /// Constructor
  const MondrianChildView({this.surface, this.interactable = true});

  /// If true then ChildView hit tests will go through
  final bool interactable;

  final Surface surface;

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: surface,
        builder: (BuildContext context, Widget child) =>
            surface.transitionModel.opacity != 1.0
                ? Stack(
                    children: <Widget>[
                      ChildView(
                        connection: surface.connection,
                        hitTestable: interactable,
                      ),
                      BreathingPlaceholder()
                    ],
                  )
                : ChildView(
                    connection: surface.connection,
                    hitTestable: interactable,
                  ),
      );

// Convert from HTML color code format to 0xAARRGGBB.
// Placeholder colors are specified as and checked for conformance as HTML to
// HTML color codes format (see: peridot/build/module_manifest_schema.json)
//
// Defaults to white in cases where no hexColorString or an invalid
// hexColorString are presented.
  Color getColor(String hexColorString) {
    Color placeholderColor = Colors.white;
    if (hexColorString != null && hexColorString.isNotEmpty) {
      try {
        String parseString = hexColorString.replaceAll('#', '');
        int colorInt = int.parse(parseString, radix: 16);
        placeholderColor = Color(colorInt).withOpacity(1.0);
      } on FormatException {
        log.fine('hexColorString conversion error for string $hexColorString');
      }
    }
    return placeholderColor;
  }
}
