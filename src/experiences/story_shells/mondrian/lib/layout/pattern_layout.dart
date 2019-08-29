// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// import 'dart:math';

import 'package:flutter/widgets.dart';
import 'package:fuchsia_logger/logger.dart';

import '../models/layout_model.dart';
import '../models/surface/positioned_surface.dart';
import '../models/surface/surface.dart';

const String _parentId = 'parent';
const String _tickerPattern = 'ticker';
const String _commentsRightPattern = 'comments-right';

const double _tickerHeightRatio = 0.15;
const double _commentsWidthRatio = 0.30;

/// Returns in the order they should stacked
List<PositionedSurface> layoutSurfaces(
  BuildContext context,
  List<Surface> focusStack,
  LayoutModel layoutModel,
) {
  if (focusStack.isEmpty) {
    return <PositionedSurface>[];
  }

  final List<PositionedSurface> layout = <PositionedSurface>[];
  Surface focused = focusStack.last;
  String pattern = focused.compositionPattern;

  if (!_isSupportedPattern(pattern)) {
    log.warning('unrecognized pattern $pattern');

    layout.add(
      PositionedSurface(
        surface: focused,
        position: Rect.fromLTWH(0.0, 0.0, 1.0, 1.0),
      ),
    );
    return layout;
  }

  Map<String, Surface> patternSurfaces = <String, Surface>{};
  // This is really a list not a stack. Reverse it to get to the 'top' items first.
  for (Surface surface in focusStack.reversed) {
    if (surface.compositionPattern != null &&
        surface.compositionPattern.isNotEmpty) {
      String pattern = surface.compositionPattern;
      patternSurfaces.putIfAbsent(pattern, () => surface);
    } else {
      // TODO (jphsiao): Once we have better signals for figuring out which module
      // to compose the pattern module with we can identify the 'source' more definitively.
      // For now, the surface without a pattern is likely the source.
      patternSurfaces.putIfAbsent(_parentId, () => surface);
    }
  }

  if (patternSurfaces.containsKey(_commentsRightPattern)) {
    // Comments-right gets laid out first
    _layoutCommentsRight(
      patternSurfaces[_parentId],
      patternSurfaces[_commentsRightPattern],
    ).forEach(layout.add);
  }
  if (patternSurfaces.containsKey(_tickerPattern)) {
    double availableWidth = 1.0;
    double availableHeight = 1.0;
    if (layout.isNotEmpty && layout[0].surface == patternSurfaces[_parentId]) {
      availableHeight = layout[0].position.height;
      availableWidth = layout[0].position.width;
    }
    List<PositionedSurface> tickerSurfaces = _layoutTicker(
      patternSurfaces[_parentId],
      patternSurfaces[_tickerPattern],
      availableHeight,
      availableWidth,
    );
    if (layout.isNotEmpty) {
      layout[0] = tickerSurfaces[0];
    } else {
      layout.add(tickerSurfaces[0]);
    }
    layout.add(tickerSurfaces[1]);
  }
  return layout;
}

bool _isSupportedPattern(String pattern) {
  return pattern == _tickerPattern || pattern == _commentsRightPattern;
}

List<PositionedSurface> _layoutTicker(Surface tickerSource, Surface ticker,
    double availableHeight, double availableWidth) {
  return <PositionedSurface>[
    PositionedSurface(
      surface: tickerSource,
      position: Rect.fromLTWH(
        0.0,
        0.0,
        availableWidth,
        availableHeight * (1.0 - _tickerHeightRatio),
      ),
    ),
    PositionedSurface(
      surface: ticker,
      position: Rect.fromLTWH(
        0.0,
        availableHeight * (1.0 - _tickerHeightRatio),
        availableWidth,
        availableHeight * _tickerHeightRatio,
      ),
    ),
  ];
}

List<PositionedSurface> _layoutCommentsRight(
  Surface commentsSource,
  Surface comments,
) {
  return <PositionedSurface>[
    PositionedSurface(
      surface: commentsSource,
      position: Rect.fromLTWH(
        0.0,
        0.0,
        1.0 - _commentsWidthRatio,
        1.0,
      ),
    ),
    PositionedSurface(
      surface: comments,
      position: Rect.fromLTWH(
        1.0 - _commentsWidthRatio,
        0.0,
        _commentsWidthRatio,
        1.0,
      ),
    ),
  ];
}
