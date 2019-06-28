// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds bar visualization given a value, fill amount, and maximum amount.
class StatusProgressBarVisualizer extends StatelessWidget {
  // Determines style of text in visualiation.
  final TextStyle textStyle;
  // Determines alignment of text on the side of the bar visualization.
  final TextAlign textAlignment;
  // Model to manage data for StatusProgressBarVisualizer.
  final StatusProgressBarVisualizerModel model;

  const StatusProgressBarVisualizer({
    @required this.model,
    this.textStyle,
    this.textAlignment,
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
    return Flex(
      direction: Axis.horizontal,
      children: <Widget>[
        // Progress Bar
        SizedBox(width: model.offset),
        Expanded(
          flex: (model.barSize * 100).toInt(),
          child: Flex(
            direction: Axis.horizontal,
            children: <Widget>[
              Expanded(
                  flex: ((model.barFill / model.barMax) * 100).toInt(),
                  child: Container(
                    height: model.barHeight,
                    color: Colors.white,
                  )),
              Expanded(
                flex: (((model.barMax - model.barFill) / model.barMax) * 100)
                    .toInt(),
                child: Container(
                  height: model.barHeight,
                  color: Colors.grey[600],
                ),
              ),
            ],
          ),
        ),
        // Descriptive Text
        Expanded(
          flex: ((1 - model.barSize) * 100).toInt(),
          child: Text(
            model.barValue,
            textAlign: textAlignment,
            style: textStyle,
          ),
        ),
      ],
    );
  }

  Widget _buildBarRight(BuildContext context) {
    return Flex(
      direction: Axis.horizontal,
      children: <Widget>[
        // Descriptive Text
        Expanded(
          flex: ((1 - model.barSize) * 100).toInt(),
          child: Text(
            model.barValue,
            textAlign: textAlignment,
            style: textStyle,
          ),
        ),
        // Progress Bar
        Expanded(
          flex: (model.barSize * 100).toInt(),
          child: Flex(
            direction: Axis.horizontal,
            children: <Widget>[
              Expanded(
                  flex: ((model.barFill / model.barMax) * 100).toInt(),
                  child: Container(
                    height: model._barHeight,
                    color: Colors.white,
                  )),
              Expanded(
                flex: (((model.barMax - model.barFill) / model.barMax) * 100)
                    .toInt(),
                child: Container(
                  height: model.barHeight,
                  color: Colors.grey[600],
                ),
              ),
            ],
          ),
        ),
        SizedBox(width: model.offset),
      ],
    );
  }
}

class StatusProgressBarVisualizerModel extends ChangeNotifier {
  // Descriptive text displayed on the side of the progress bar visualization.
  String _barValue;
  // Amount the progress bar visualization will be filled.
  double _barFill;
  // Maximum amount the progress bar visualization can be filled.
  double _barMax;
  // Double between 0 - 1 that determines the ratio of bar to text in container.
  double _barSize;
  // Height of bar visualization & text.
  double _barHeight;
  // Determines if bar visualization is first in order in row.
  bool _barFirst;
  // Offsets bar from alignment axis.
  double _offset;

  set barValue(String updatedBarValue) {
    _barValue = updatedBarValue;
    notifyListeners();
  }

  set barFill(double updatedBarFill) {
    _barFill = updatedBarFill;
    notifyListeners();
  }

  set barMax(double updatedBarMax) {
    _barMax = updatedBarMax;
    notifyListeners();
  }

  String get barValue => _barValue;

  double get barFill => _barFill;

  double get barMax => _barMax;

  double get barSize => _barSize;

  double get barHeight => _barHeight;

  bool get barFirst => _barFirst;

  double get offset => _offset;

  StatusProgressBarVisualizerModel({
    String barValue = 'loading...',
    double barFill = 0,
    double barMax = 1,
    double barSize = .5,
    double barHeight = 14,
    bool barFirst = true,
    double offset = 0,
  })  : _barValue = barValue,
        _barFill = barFill,
        _barMax = barMax,
        _barSize = barSize,
        _barHeight = barHeight,
        _barFirst = barFirst,
        _offset = offset;
}
