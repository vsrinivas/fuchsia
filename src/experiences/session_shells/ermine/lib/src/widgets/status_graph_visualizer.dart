// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds graph to visualize a list/stream of data.
class StatusGraphVisualizer extends StatelessWidget {
  // Descriptive text displayed on the side of the graph visualization.
  final String _graphValue;
  // Data to be represented in graph.
  final List<double> _graphData;
  // Height of graph widget.
  final double _graphHeight;
  // Width of graph widget.
  final double _graphWidth;
  // Min data value found within data being plotted.
  final double _graphMin;
  // Max data value found within data being plotted.
  final double _graphMax;
  // Determines style of text in visualiation.
  final TextStyle _textStyle;
  // Determines alignment of text/graph in visualization.
  final MainAxisAlignment _axisAlignment;
  // Determines if graph visualization is first in order in row.
  final bool _graphFirst;
  // If true, draws border around graph.
  final bool _borderActive;
  // Determines paint style used to draw graph.
  final Paint _drawStyle;
  // Determines if graph is filled underneath.
  final bool _fillActive;

  const StatusGraphVisualizer(
      {@required String graphValue,
      @required graphData,
      @required double graphHeight,
      @required double graphWidth,
      @required double graphMin,
      @required double graphMax,
      @required TextStyle textStyle,
      @required MainAxisAlignment axisAlignment,
      @required bool graphFirst,
      @required bool borderActive,
      @required Paint drawStyle,
      @required bool fillActive})
      : _graphValue = graphValue,
        _graphData = graphData,
        _graphHeight = graphHeight,
        _graphWidth = graphWidth,
        _graphMin = graphMin,
        _graphMax = graphMax,
        _textStyle = textStyle,
        _axisAlignment = axisAlignment,
        _graphFirst = graphFirst,
        _borderActive = borderActive,
        _drawStyle = drawStyle,
        _fillActive = fillActive;

  @override
  Widget build(BuildContext context) {
    if (_graphFirst)
      return _buildGraphLeft(context);
    return _buildGraphRight(context);
  }

  Widget _buildGraphLeft(BuildContext context) {
    return Row(
      mainAxisAlignment: _axisAlignment,
      children: [
        SizedBox(
            height: _graphHeight,
            width: _graphWidth,
            child: _buildGraph(context)),
        Text(
          _graphValue,
          style: _textStyle,
        ),
      ],
    );
  }

  Widget _buildGraphRight(BuildContext context) {
    return Row(
      mainAxisAlignment: _axisAlignment,
      children: [
        Text(
          _graphValue,
          style: _textStyle,
        ),
        SizedBox(
            height: _graphHeight,
            width: _graphWidth,
            child: _buildGraph(context)),
      ],
    );
  }

  Widget _buildGraph(BuildContext context) {
    return CustomPaint(
      painter: _StatusGraphPainter(
          data: _graphData,
          height: _graphHeight,
          width: _graphWidth,
          min: _graphMin,
          max: _graphMax,
          drawStyle: _drawStyle,
          borderActive: _borderActive,
          fillActive: _fillActive),
    );
  }
}

class _StatusGraphPainter extends CustomPainter {
  List<double> data;
  double height;
  double width;
  double borderHeight;
  double borderWidth;
  double min;
  double max;
  double xFactor;
  double yFactor;
  bool borderActive;
  bool fillActive;
  Path path = Path();
  Offset start;
  List<Offset> points = <Offset>[];
  List<Offset> border = <Offset>[];
  Paint drawStyle;

  _StatusGraphPainter(
      {this.data,
      this.height,
      this.width,
      this.min,
      this.max,
      this.drawStyle,
      this.borderActive,
      this.fillActive});

  @override
  void paint(Canvas canvas, Size size) {
    xFactor = _getXFactor(data.length.toDouble(), width);
    yFactor = _getYFactor(min, max, height);
    borderWidth = width - drawStyle.strokeWidth;
    borderHeight = height - drawStyle.strokeWidth;
    start = Offset(0, (height - data[0] * yFactor + drawStyle.strokeWidth));

    for (int index = 0; index < data.length; index++) {
      double x = index.toDouble() * xFactor;
      double y = height - data[index] * yFactor;
      points.add(Offset(x, y));
      debugPrint(points[index].toString());
    }

    if (borderActive) {
      border
        ..add(Offset(0, 0))
        ..add(Offset(0, borderHeight))
        ..add(Offset(borderWidth, borderHeight))
        ..add(Offset(borderWidth, 0))
        ..add(Offset(0, 0));
      canvas.drawPoints(PointMode.polygon, border, drawStyle);
    }

    if (fillActive) {
      Path fillPath = Path()
        ..addPath(path, Offset.zero)
        ..moveTo(start.dx, start.dy);

      for (int index = 0; index < data.length; index++) {
        double x = index.toDouble() * xFactor;
        double y = height - data[index] * yFactor;
        fillPath.lineTo(x, y);
      }

      fillPath
        ..lineTo(width, height)
        ..close();

      canvas.drawPath(fillPath, drawStyle);
    }
    canvas.drawPoints(PointMode.polygon, points, drawStyle);
  }

  @override
  bool shouldRepaint(_StatusGraphPainter prev) {
    return data != prev.data ||
        height != prev.height ||
        width != prev.width ||
        min != prev.min ||
        max != prev.max ||
        drawStyle != prev.drawStyle ||
        borderActive != borderActive ||
        fillActive != prev.fillActive;
  }

  double _getXFactor(double totalDataPoints, double graphWidth) =>
      (graphWidth / (totalDataPoints - 1));

  double _getYFactor(double min, double max, double height) =>
      (height / (max - min));
}
