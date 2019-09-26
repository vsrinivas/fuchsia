// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter/material.dart';

import '../../models/cluster_model.dart';
import '../../utils/styles.dart';

class Thumbnails extends StatelessWidget {
  static const _kThumbnailMinWidth = ErmineStyle.kRecentsItemWidth;
  static const _kThumbnailAspectRatio = 1;
  static const _kThumbnailOverviewPadding = 32.0;

  final bool overview;
  final ClustersModel model;

  const Thumbnails({@required this.model, this.overview = false});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      final padding = overview ? _kThumbnailOverviewPadding : 0;
      final itemSize = _itemSize(
        availableSize: constraints.biggest,
        items: model.stories.length,
        padding: padding / 2,
      );
      return SingleChildScrollView(
        padding: EdgeInsets.all(padding / 2),
        child: Wrap(
          children: model.stories
              .map((story) => GestureDetector(
                    child: Container(
                      padding: EdgeInsets.all(padding / 2),
                      width: itemSize.width,
                      height: itemSize.height,
                      child: Container(
                        alignment: Alignment.center,
                        padding: EdgeInsets.all(8),
                        decoration: BoxDecoration(
                          color: ErmineStyle.kOverlayBackgroundColor,
                          border: Border.all(
                            color: ErmineStyle.kOverlayBorderColor,
                          ),
                        ),
                        child: Text(
                          story.name,
                          style: TextStyle(color: Colors.white),
                        ),
                      ),
                    ),
                    onTap: story.focus,
                  ))
              .toList(),
        ),
      );
    });
  }

  Size _itemSize({
    Size availableSize,
    int items,
    double padding = 0,
  }) {
    if (!overview) {
      return Size(
          _kThumbnailMinWidth, _kThumbnailMinWidth / _kThumbnailAspectRatio);
    }
    final columns = _isSquare(items) ? sqrt(items) : sqrt(items).ceil();
    double availableWidth = availableSize.width;
    double width = (availableWidth - (columns + 1) * padding) / columns;
    width = max(width, _kThumbnailMinWidth);
    double height = width / _kThumbnailAspectRatio;
    return Size(width, height);
  }

  bool _isSquare(int n) {
    final root = sqrt(n);
    return root - root.floor() == 0;
  }
}
