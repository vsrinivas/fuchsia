// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

import '../models/status_model.dart';
import '../utils/elevations.dart';
import '../widgets/status_tick_bar_visualizer.dart';

const _listItemHeight = 35.0;
const _leadingStyle = TextStyle(
  color: Colors.white,
  fontSize: 14,
  fontFamily: 'RobotoMono',
  fontWeight: FontWeight.w700,
);
const _titleStyle = TextStyle(
  color: Colors.white,
  fontSize: 14,
  wordSpacing: 1,
  fontFamily: 'RobotoMono',
  fontWeight: FontWeight.w400,
);

/// Builds the display for the Status menu.
class Status extends StatelessWidget {
  final StatusModel model;

  const Status({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.white,
      elevation: elevations.systemOverlayElevation,
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Container(
          color: Colors.black,
          child: ListView(
            physics: const NeverScrollableScrollPhysics(),
            children: <Widget>[
              // Date
              SizedBox(
                height: _listItemHeight * 1.4,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    model.getDate(),
                    textAlign: TextAlign.left,
                    style: _titleStyle,
                  ),
                ),
              ),
              // Wifi
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'Wifi',
                    style: _leadingStyle,
                  ),
                  title: Text(
                    model.getWireless(),
                    textAlign: TextAlign.right,
                    style: _titleStyle,
                  ),
                ),
              ),
              // CPU Usage
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'CPU Usage',
                    style: _leadingStyle,
                  ),
                  title: StatusTickBarVisualizer(
                    barValue: model.getCpu(),
                    barFill: model.getCpuFill(),
                    barMax: model.getCpuMax(),
                    tickMax: 25,
                    textStyle: _titleStyle,
                    textAlignment: TextAlign.left,
                    barFirst: true,
                  ),
                ),
              ),
              // Memory Usage
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'Mem Usage',
                    style: _leadingStyle,
                  ),
                  title: StatusTickBarVisualizer(
                    barValue: model.getMem(),
                    barFill: model.getMemFill(),
                    barMax: model.getMemMax(),
                    tickMax: 25,
                    textStyle: _titleStyle,
                    textAlignment: TextAlign.left,
                    barFirst: true,
                  ),
                ),
              ),
              // Tasks
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'Tasks',
                    style: _leadingStyle,
                  ),
                  title: Text(
                    model.getTasks(),
                    textAlign: TextAlign.right,
                    style: _titleStyle,
                  ),
                ),
              ),
              // Weather
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'Weather',
                    style: _leadingStyle,
                  ),
                  title: Text(
                    model.getWeather(),
                    textAlign: TextAlign.right,
                    style: _titleStyle,
                  ),
                ),
              ),
              // Battery
              SizedBox(
                height: _listItemHeight,
                child: ListTile(
                  dense: true,
                  leading: Text(
                    'Battery',
                    style: _leadingStyle,
                  ),
                  title: Text(
                    model.getBattery(),
                    textAlign: TextAlign.right,
                    style: _titleStyle,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}