// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds bar visualization given a value, fill amount, and maximum amount.
class StatusTickBarVisualizer extends StatelessWidget {
  // Determines style of text in visualiation.
  final TextStyle textStyle;
  // Determines alignment of text on the side of the bar visualization.
  final TextAlign textAlignment;
  // Model to manage data for StatusTickBarVisualizerModel.
  final StatusTickBarVisualizerModel model;

  const StatusTickBarVisualizer({
    @required this.model,
    @required this.textStyle,
    @required this.textAlignment,
  });

  @override
  Widget build(BuildContext context) {
    if (model.barFirst)
      return AnimatedBuilder(
          animation: model,
          builder: (BuildContext context, Widget child) {
            return _buildBarLeft(context);
          });
    return AnimatedBuilder(
        animation: model,
        builder: (BuildContext context, Widget child) {
          return _buildBarRight(context);
        });
  }

  Widget _buildBarLeft(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: <Widget>[
        Text(_drawTicks(_activeTicks()), style: _setTickStyle(true)),
        Text(
          _drawTicks(_inactiveTicks()),
          style: _setTickStyle(false),
        ),
        SizedBox(width: _barSpace()),
        Text(
          model.barValue,
          textAlign: textAlignment,
          style: textStyle,
        ),
      ],
    );
  }

  Widget _buildBarRight(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: <Widget>[
        Text(
          model.barValue,
          textAlign: textAlignment,
          style: textStyle,
        ),
        SizedBox(width: _barSpace()),
        Text(
          _drawTicks(_activeTicks()),
          style: _setTickStyle(true),
        ),
        Text(_drawTicks(_inactiveTicks()), style: _setTickStyle(false)),
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
  int _activeTicks() {
    return ((model.barFill / model.barMax) * _maxTicks()).toInt();
  }

  // Determines how many inactive ticks to be drawn.
  int _inactiveTicks() => (_maxTicks() - _activeTicks());

  // Determines how many ticks can fit in row.
  int _maxTicks() => model.tickMax - model.barValue.length;

  // Builds string of ticks.
  String _drawTicks(int numTicks) {
    if (numTicks < 0 || numTicks == null) {
      return List.filled(1, '').join('| ');
    }
    return List.filled(numTicks + 1, '').join('| ');
  }

  // Adds space to align bar visualizations.
  double _barSpace() => model.barValue.length % 2 == 0 ? 9 : 6;
}

class StatusTickBarVisualizerModel extends ChangeNotifier {
  // Descriptive text displayed to the right of bar visualization.
  String _barValue;
  // Amount the bar visualization will be filled.
  double _barFill;
  // Maximum amount the bar visualization can be filled.
  double _barMax;
  // Maximum amount of tick marks allowed to be in row.
  final int _tickMax;
  // Determines if bar visualization is first in order in row.
  final bool _barFirst;

  set barFill(double updatedBarFill) {
    _barFill = updatedBarFill;
    notifyListeners();
  }

  set barMax(double updatedBarMax) {
    _barMax = updatedBarMax;
    notifyListeners();
  }

  set barValue(String updatedBarValue) {
    _barValue = updatedBarValue;
    notifyListeners();
  }

  String get barValue => _barValue;

  double get barFill => _barFill;

  double get barMax => _barMax;

  int get tickMax => _tickMax;

  bool get barFirst => _barFirst;

  StatusTickBarVisualizerModel({
    String barValue = 'loading...',
    double barFill = 1,
    double barMax = 2,
    int tickMax = 25,
    bool barFirst = true,
  })  : _barValue = barValue,
        _barFill = barFill,
        _barMax = barMax,
        _tickMax = tickMax,
        _barFirst = barFirst;
}
