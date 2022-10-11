// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';
import '../../visual_languages/colors.dart';
import '../../visual_languages/text_styles.dart';
import 'layout.dart';

/// A placeholder UI showing that a process such as loading is happening now.
///
/// It consists of 8 dots with colors changing in rotation.
class LoadingIndicator extends StatefulWidget {
  /// If non-null, the graphical indicator has the text below it.
  final String description;

  /// If non-null, the duration for updating the color index is set to this.
  /// Otherwise, it is set to [kDefaultSpeed] by default.
  final int speedMs;

  // TODO(fxb/72867): Add factories providing different sizes.
  const LoadingIndicator(
      {this.description = '', this.speedMs = kDefaultSpeed, Key? key})
      : super(key: key);

  @override
  LoadingIndicatorState createState() => LoadingIndicatorState();
}

class LoadingIndicatorState extends State<LoadingIndicator> {
  final _firstColorIndex = ValueNotifier(0);
  Timer? _timer;

  final _colors = [
    ErmineColors.white,
    ErmineColors.grey100,
    ErmineColors.grey200,
    ErmineColors.grey300,
    ErmineColors.grey400,
    ErmineColors.grey400,
    ErmineColors.grey500,
    ErmineColors.grey500
  ];

  @visibleForTesting
  int get firstColorIndex => _firstColorIndex.value;

  @override
  void initState() {
    _timer = _setTimer();
    super.initState();
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Timer _setTimer() =>
      Timer(Duration(milliseconds: widget.speedMs), _updateColorIndex);

  void _updateColorIndex() {
    final nextIndex = _firstColorIndex.value - 1;
    _firstColorIndex.value =
        (nextIndex < 0) ? _colors.length + nextIndex : nextIndex;
    _timer = _setTimer();
  }

  @override
  Widget build(BuildContext context) => (widget.description.isEmpty)
      ? _buildCircularDots()
      : Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            _buildCircularDots(),
            SizedBox(height: 16.0),
            Text(widget.description, style: ErmineTextStyles.bodyText1),
          ],
        );

  Widget _buildCircularDots() => Container(
        margin: kIndicatorMargin,
        child: Stack(
          children: [
            for (var i = 0; i < _colors.length; i++)
              Transform(
                alignment: FractionalOffset.center,
                transform: Matrix4.identity()
                  ..rotateZ((i * kDotRotateAngle) * 3.1415927 / 180)
                  ..translate(0.0, kDotTranslateVectorY, 0.0),
                child: ValueListenableBuilder(
                  valueListenable: _firstColorIndex,
                  builder: (BuildContext context, int firstColorIndex,
                          Widget? child) =>
                      _Dot(_getColor(i, firstColorIndex)),
                ),
              ),
          ],
        ),
      );

  Color _getColor(int dotIndex, int firstColorIndex) {
    final dotColorIndex = firstColorIndex + dotIndex;
    if (dotColorIndex < _colors.length) {
      return _colors[dotColorIndex];
    }
    return _colors[dotColorIndex - _colors.length];
  }
}

class _Dot extends StatelessWidget {
  final Color _color;
  const _Dot(this._color);

  @override
  Widget build(BuildContext context) => Container(
        width: kDotSize,
        height: kDotSize,
        decoration: BoxDecoration(
          color: _color,
          shape: BoxShape.circle,
        ),
      );
}
