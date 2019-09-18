// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

import '../../models/status_model.dart';
import 'status_graph_visualizer.dart';
import 'status_grid_visualizer.dart';
import 'status_progress_bar_visualizer.dart';

const _listItemHeight = 28.0;
const _statusTextStyle = TextStyle(
  color: Colors.white,
  fontSize: 11,
  letterSpacing: 0,
  fontFamily: 'RobotoMono',
  fontWeight: FontWeight.w400,
);
const _statusTextStyleBlack = TextStyle(
  color: Colors.black,
  fontSize: 11,
  letterSpacing: 0,
  fontFamily: 'RobotoMono',
  fontWeight: FontWeight.w400,
);
Paint _fillPaint = Paint()
  ..strokeWidth = 0.5
  ..color = Colors.white
  ..style = PaintingStyle.fill;

/// Builds the display for the Status menu.
class Status extends StatelessWidget {
  final StatusModel model;

  const Status({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Container(
      child: ListView(
        physics: const NeverScrollableScrollPhysics(),
        children: [
          // Buttons
          Container(
            child: Row(
              mainAxisAlignment: MainAxisAlignment.start,
              children: <Widget>[
                Container(
                  height: 14,
                  width: 37,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'SLEEP',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
                SizedBox(
                  width: 20,
                ),
                Container(
                  height: 14,
                  width: 50,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'RESTART',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
                SizedBox(
                  width: 20,
                ),
                Container(
                  height: 14,
                  width: 63,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'POWER OFF',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
                SizedBox(
                  width: 90,
                ),
                Container(
                  height: 14,
                  width: 64,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'SETTINGS',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: model.launchSettings,
                  ),
                ),
              ],
            ),
          ),
          // Volume
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('VOLUME'),
            rowContent: Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: <Widget>[
                Container(
                  width: 150,
                  child: StatusProgressBarVisualizer(
                    model: model.dummyVolumeModel,
                    textAlignment: TextAlign.center,
                    textStyle: _statusTextStyle,
                  ),
                ),
                SizedBox(
                  width: 10,
                ),
                Container(
                  height: 14,
                  width: 24,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'MIN',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
                SizedBox(
                  width: 10,
                ),
                Container(
                  height: 14,
                  width: 24,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'MAX',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
              ],
            ),
          ),
          // Brightness
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('BRIGHTNESS'),
            rowContent: Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: <Widget>[
                Container(
                  width: 150,
                  child: StatusProgressBarVisualizer(
                    model: model.dummyBrightnessModel,
                    textAlignment: TextAlign.center,
                    textStyle: _statusTextStyle,
                  ),
                ),
                SizedBox(
                  width: 10,
                ),
                Container(
                  height: 14,
                  width: 24,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'MIN',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
                SizedBox(
                  width: 10,
                ),
                Container(
                  height: 14,
                  width: 24,
                  color: Colors.white,
                  child: FlatButton(
                    materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    padding: EdgeInsets.all(0),
                    child: Text(
                      'MAX',
                      style: _statusTextStyleBlack,
                      textAlign: TextAlign.left,
                    ),
                    onPressed: null,
                  ),
                ),
              ],
            ),
          ),
          // Music Player
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('MUSIC'),
            rowContent: Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: <Widget>[
                  Container(
                    height: 14,
                    width: 31,
                    color: Colors.white,
                    child: FlatButton(
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                      padding: EdgeInsets.all(0),
                      child: Text(
                        'BACK',
                        style: _statusTextStyleBlack,
                        textAlign: TextAlign.left,
                      ),
                      onPressed: null,
                    ),
                  ),
                  SizedBox(
                    width: 10,
                  ),
                  Container(
                    height: 14,
                    width: 38,
                    color: Colors.white,
                    child: FlatButton(
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                      padding: EdgeInsets.all(0),
                      child: Text(
                        'PAUSE',
                        style: _statusTextStyleBlack,
                        textAlign: TextAlign.left,
                      ),
                      onPressed: null,
                    ),
                  ),
                  SizedBox(
                    width: 10,
                  ),
                  Container(
                    height: 14,
                    width: 31,
                    color: Colors.white,
                    child: FlatButton(
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                      padding: EdgeInsets.all(0),
                      child: Text(
                        'SKIP',
                        style: _statusTextStyleBlack,
                        textAlign: TextAlign.left,
                      ),
                      onPressed: null,
                    ),
                  ),
                ]),
          ),
          // Processes
          StatusGridVisualizer(
            model: StatusGridVisualizerModel(),
            textStyle: _statusTextStyle,
            title: _packageTitleText('TOP PROCESSES'),
            titleHeight: _listItemHeight,
          ),
          // Memory
          _RowItem(
              height: _listItemHeight,
              rowTitle: _packageTitleText('MEMORY'),
              rowContent: StatusProgressBarVisualizer(
                model: model.memoryModel,
                textAlignment: TextAlign.right,
                textStyle: _statusTextStyle,
              )),
          // Battery
          // _RowItem(
          //     height: _listItemHeight,
          //     rowTitle: _packageTitleText('BATT'),
          //     rowContent: StatusProgressBarVisualizer(
          //       model: model.batteryModel,
          //       textAlignment: TextAlign.right,
          //       textStyle: _statusTextStyle,
          //     )),
          // CPU Usage
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('CPU'),
            rowContent: StatusGraphVisualizer(
              textStyle: _statusTextStyle,
              drawStyle: _fillPaint,
              axisAlignment: MainAxisAlignment.spaceBetween,
              model: model.dummyCpuModel,
            ),
          ),
          // Tasks
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('TASKS'),
            rowContent: _packageContentText(model.getTasks()),
          ),
          // Weather
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('WEATHER'),
            rowContent: _packageContentText(model.getWeather()),
          ),
          // Date
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('DATE'),
            rowContent: _packageContentText(model.getDate()),
          ),
          // Network
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('NETWORK'),
            rowContent: _packageContentText(model.getNetwork()),
          ),
          // FPS
          _RowItem(
            height: _listItemHeight,
            rowTitle: _packageTitleText('FPS'),
            rowContent: _packageContentText(model.getFps()),
          ),
        ],
      ),
    );
  }

  Widget _packageTitleText(String title) =>
      Text(title, style: _statusTextStyle);

  Widget _packageContentText(String title) =>
      Text(title, style: _statusTextStyle, textAlign: TextAlign.right);
}

class _RowItem extends StatelessWidget {
  final double height;
  final Widget rowTitle;
  final Widget rowContent;

  const _RowItem({this.height, this.rowTitle, this.rowContent});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: height,
      child: ListTile(
        dense: true,
        leading: rowTitle,
        title: rowContent,
        contentPadding: EdgeInsets.zero,
      ),
    );
  }
}
