// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds bar visualization given a value, fill amount, and maximum amount.
class StatusProgressBarVisualizer extends StatelessWidget {
  // Descriptive text displayed on the side of the progress bar visualization.
  final String _barValue;
  // Amount the progress bar visualization will be filled.
  final double _barFill;
  // Maximum amount the progress bar visualization can be filled.
  final double _barMax;
  // Double between 0 - 1 that determines the ratio of bar to text in container.
  final double _barSize;
  // Height of bar visualization & text.
  final double _barHeight;
  // Determines style of text in visualiation.
  final TextStyle _textStyle;
  // Determines alignment of text on the side of the bar visualization.
  final TextAlign _textAlignment;
  // Determines if bar visualization is first in order in row.
  final bool _barFirst;

  const StatusProgressBarVisualizer(
      {@required String barValue,
      @required double barFill,
      @required double barMax,
      @required double barSize,
      @required double barHeight,
      @required TextStyle textStyle,
      @required TextAlign textAlignment,
      @required bool barFirst})
      : _barValue = barValue,
        _barFill = barFill,
        _barMax = barMax,
        _barSize = barSize,
        _barHeight = barHeight,
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
    return Flex(
      direction: Axis.horizontal,
      children: <Widget>[
        // Progress Bar
        Expanded(
          flex: (_barSize * 100).toInt(),
          child: Flex(
            direction: Axis.horizontal,
            children: <Widget>[
              Expanded(
                  flex: ((_barFill / _barMax) * 100).toInt(),
                  child: Container(
                    height: _barHeight,
                    color: Colors.white,
                  )),
              Expanded(
                flex: (((_barMax - _barFill) / _barMax) * 100).toInt(),
                child: Container(
                  height: _barHeight,
                  color: Colors.grey[600],
                ),
              ),
            ],
          ),
        ),
        // Descriptive Text
        Expanded(
          flex: ((1 - _barSize) * 100).toInt(),
          child: Text(
            _barValue,
            textAlign: _textAlignment,
            style: _textStyle,
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
          flex: ((1 - _barSize) * 100).toInt(),
          child: Text(
            _barValue,
            textAlign: _textAlignment,
            style: _textStyle,
          ),
        ),
        // Progress Bar
        Expanded(
          flex: (_barSize * 100).toInt(),
          child: Flex(
            direction: Axis.horizontal,
            children: <Widget>[
              Expanded(
                  flex: ((_barFill / _barMax) * 100).toInt(),
                  child: Container(
                    height: _barHeight,
                    color: Colors.white,
                  )),
              Expanded(
                flex: (((_barMax - _barFill) / _barMax) * 100).toInt(),
                child: Container(
                  height: _barHeight,
                  color: Colors.grey[600],
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
