// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'dart:ui';

import 'package:flutter/material.dart';

// Builds graph to visualize a list/stream of data.
class StatusGraphVisualizer extends StatelessWidget {
  // Determines style of text in visualiation.
  final TextStyle textStyle;
  // Determines alignment of text/graph in visualization.
  final MainAxisAlignment axisAlignment;
  // Determines paint style used to draw graph.
  final Paint drawStyle;
  // Model to manage data for StatusGraphVisualizer.
  final StatusGraphVisualizerModel model;

  const StatusGraphVisualizer({
    @required this.model,
    @required this.textStyle,
    @required this.axisAlignment,
    @required this.drawStyle,
  });

  @override
  Widget build(BuildContext context) {
    if (model.graphFirst)
      return AnimatedBuilder(
          animation: model,
          builder: (BuildContext context, Widget child) {
            return _buildGraphLeft(context);
          });
    return AnimatedBuilder(
        animation: model,
        builder: (BuildContext context, Widget child) {
          return _buildGraphRight(context);
        });
  }

  Widget _buildGraphLeft(BuildContext context) {
    return Row(
      mainAxisAlignment: axisAlignment,
      children: [
        SizedBox(
            height: model.graphHeight,
            width: model.graphWidth,
            child: _buildGraph(context)),
        Text(
          model.graphValue,
          style: textStyle,
        ),
      ],
    );
  }

  Widget _buildGraphRight(BuildContext context) {
    return Row(
      mainAxisAlignment: axisAlignment,
      children: [
        Text(
          model.graphValue,
          style: textStyle,
        ),
        SizedBox(
            height: model.graphHeight,
            width: model.graphWidth,
            child: _buildGraph(context)),
      ],
    );
  }

  Widget _buildGraph(BuildContext context) {
    return CustomPaint(
      painter: _StatusGraphPainter(
          data: model._graphDataList,
          height: model.graphHeight,
          width: model.graphWidth,
          min: model.graphMin,
          max: model.graphMax,
          drawStyle: drawStyle,
          borderActive: model.borderActive,
          fillActive: model.fillActive),
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

  _StatusGraphPainter({
    this.data,
    this.height,
    this.width,
    this.min,
    this.max,
    this.drawStyle,
    this.borderActive,
    this.fillActive,
  });

  @override
  void paint(Canvas canvas, Size size) {
    xFactor = _getXFactor(data.length.toDouble(), width);
    yFactor = _getYFactor(min, max, height);
    borderWidth = width;
    borderHeight = height;
    start = Offset(0, height);

    for (int index = 0; index < data.length; index++) {
      double x = index.toDouble() * xFactor;
      double y = height - data[index] * yFactor;
      points.add(Offset(x, y));
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

class StatusGraphVisualizerModel extends ChangeNotifier {
  // Descriptive text displayed on the side of the graph visualization.
  String _graphValue;
  // Data to be represented in graph.
  double _graphData;
  // Height of graph widget.
  double _graphHeight;
  // Width of graph widget.
  double _graphWidth;
  // Min data value found within data being plotted.
  double _graphMin;
  // Max data value found within data being plotted.
  double _graphMax;
  // Determines if graph visualization is first in order in row.
  bool _graphFirst;
  // If true, draws border around graph.
  bool _borderActive;
  // Determines if graph is filled underneath.
  bool _fillActive;
  final List<double> _graphDataList = List.filled(50, 0);

  StatusGraphVisualizerModel({
    String graphValue = 'loading...',
    double graphData = 1,
    double graphHeight = 14,
    double graphWidth = 60,
    double graphMin = 0,
    double graphMax = 2,
    bool graphFirst = true,
    bool borderActive = true,
    bool fillActive = true,
  })  : _graphValue = graphValue,
        _graphData = graphData,
        _graphHeight = graphHeight,
        _graphWidth = graphWidth,
        _graphMin = graphMin,
        _graphMax = graphMax,
        _graphFirst = graphFirst,
        _borderActive = borderActive,
        _fillActive = fillActive;

  set graphData(double updatedGraphData) {
    _graphData = updatedGraphData;
    _updateGraph(_graphData);
    notifyListeners();
  }

  set graphValue(String updatedGraphValue) {
    _graphValue = updatedGraphValue;
    notifyListeners();
  }

  String get graphValue => _graphValue;

  double get graphData => _graphData;

  double get graphHeight => _graphHeight;

  double get graphWidth => _graphWidth;

  double get graphMin => _graphMin;

  double get graphMax => _graphMax;

  bool get graphFirst => _graphFirst;

  bool get borderActive => _borderActive;

  bool get fillActive => _fillActive;

  void _updateGraph(double newDataPoint) {
    double newElem = Random().nextDouble() * 100;
    for (int a = 0; a < _graphDataList.length - 1; a++) {
      _graphDataList[a] = _graphDataList[a + 1];
    }
    _graphDataList[_graphDataList.length - 1] = newElem;
  }
}
