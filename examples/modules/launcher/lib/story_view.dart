// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

import 'ui/rk4_spring_simulation.dart';
import 'ui/ticking_simulation.dart';

const double _kExtentSimulationTension = 150.0;
const double _kExtentSimulationFriction = 25.0;
const RK4SpringDescription kExtentSimulationDesc = const RK4SpringDescription(
    tension: _kExtentSimulationTension, friction: _kExtentSimulationFriction);

class SimulatedHeightWidget extends StatefulWidget {
  final Widget child;
  SimulatedHeightWidget({Key key, this.child}) : super(key: key);

  @override
  SimulatedHeightWidgetState createState() => new SimulatedHeightWidgetState();
}

class SimulatedHeightWidgetState extends State<SimulatedHeightWidget> {
  TickingSimulation _tickingSimulation;

  SimulatedHeightWidgetState() {
    _tickingSimulation = new TickingSimulation(
        simulation: new RK4SpringSimulation(
            initValue: 0.0, desc: kExtentSimulationDesc),
        onTick: rebuildWidget);
  }

  void rebuildWidget() {
    setState(() {});
  }

  set height(double height) {
    _tickingSimulation.target = height;
  }

  @override
  Widget build(_) =>
      new Container(height: _tickingSimulation.value, child: config.child);
}

class StoryView extends StatefulWidget {
  StoryView({Key key}) : super(key: key);

  @override
  StoryViewState createState() => new StoryViewState();
}

class StoryViewState extends State<StoryView> {
  List<Widget> _composeChildren = <Widget>[];
  Widget _contextWidget;

  set composeChildren(List<Widget> composeChildren) {
    setState(() {
      _composeChildren = composeChildren;
    });
  }

  set contextWidget(Widget contextWidget) {
    setState(() {
      _contextWidget = contextWidget;
    });
  }

  @override
  Widget build(_) {
    List<Widget> children = <Widget>[];
    if (_contextWidget != null) {
      children.add(_contextWidget);
    }
    children.addAll(
        _composeChildren.map((Widget b) => new Flexible(child: b)).toList());
    return new Column(children: children);
  }
}
