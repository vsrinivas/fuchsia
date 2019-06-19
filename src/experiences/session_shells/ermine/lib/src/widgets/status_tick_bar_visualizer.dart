// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds bar visualization given a value, fill amount, and maximum amount.
class StatusTickBarVisualizer extends StatelessWidget {
  // Descriptive text displayed to the right of bar visualization.
  final String _barValue;
  // Amount the bar visualization will be filled.
  final double _barFill;
  // Maximum amount the bar visualization can be filled.
  final double _barMax;
  // Maximum amount of tick marks allowed to be in row.
  final int _tickMax;
  // Determines style of text in visualiation.
  final TextStyle _textStyle;
  // Determines alignment of text on the side of the bar visualization.
  final TextAlign _textAlignment;
  // Determines if bar visualization is first in order in row.
  final bool _barFirst;

  const StatusTickBarVisualizer(
      {@required String barValue,
      @required double barFill,
      @required double barMax,
      @required int tickMax,
      @required TextStyle textStyle,
      @required TextAlign textAlignment,
      @required bool barFirst})
      : _barValue = barValue,
        _barFill = barFill,
        _barMax = barMax,
        _tickMax = tickMax,
        _textStyle = textStyle,
        _textAlignment = textAlignment,
        _barFirst = barFirst;

  @override
  Widget build(BuildContext context) {
    if (_barFirst)
      return _buildBarLeft(context);
    return _buildBarRight(context);
  }

  Widget _buildBarLeft(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: <Widget>[
        Text(
          _drawTicks(_activeTicks()),
          style: _setTickStyle(true)
        ),
        Text(
          _drawTicks(_inactiveTicks()),
          style: _setTickStyle(false),
        ),
        SizedBox(width: _barSpace()),
        Text(
          _barValue,
          textAlign: _textAlignment,
          style: _textStyle,
        ),
      ],
    );
  }

  Widget _buildBarRight(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: <Widget>[
        Text(
          _barValue,
          textAlign: _textAlignment,
          style: _textStyle,
        ),
        SizedBox(width: _barSpace()),
        Text(
          _drawTicks(_activeTicks()),
          style: _setTickStyle(true),
        ),
        Text(
          _drawTicks(_inactiveTicks()),
          style: _setTickStyle(false)
        ),
      ],
    );
  }

  TextStyle _setTickStyle(bool active) {
    if (active) {
      return TextStyle(
        color: Colors.white,
        fontSize: 14,
        letterSpacing: -4,
        fontFamily: 'RobotoMono',
        fontWeight: FontWeight.w400,
      );
    }
    return TextStyle(
      color: Colors.grey[600],
      fontSize: 14,
      letterSpacing: -4,
      fontFamily: 'RobotoMono',
      fontWeight: FontWeight.w400,
    );
  }

  // Determines how many active ticks to be drawn.
  int _activeTicks() => ((_barFill / _barMax) * _maxTicks()).toInt();

  // Determines how many inactive ticks to be drawn.
  int _inactiveTicks() => (_maxTicks() - _activeTicks());

  // Determines how many ticks can fit in row.
  int _maxTicks() => _tickMax - _barValue.length;

  // Builds string of ticks.
  String _drawTicks(int numTicks) => List.filled(numTicks + 1, '').join('| ');

  // Adds space to align bar visualizations.
  double _barSpace() => _barValue.length % 2 == 0 ? 9 : 6;
}
