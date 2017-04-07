// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import 'constraints_model.dart';
import 'rounded_corner_decoration.dart';

/// Bezel constants.  Used to give the illusion of a device.
const double _kBezelMinimumWidth = 8.0;
const double _kBezelExtension = 16.0;
const double _kOuterBezelRadius = 16.0;
const double _kInnerBezelRadius = 8.0;

/// A widget that changes [child]'s constraints to one within
/// [constraintsModel]. An affordance to perform this change is placed in
/// [ChildConstraintsChanger]'s top right.  Each tap of the affordance steps
/// through the [constraintsModel] list applying each constraint to [child] in
/// turn.
class ChildConstraintsChanger extends StatefulWidget {
  /// The model containing the constraints [child] should be constrained by.
  final ConstraintsModel constraintsModel;

  /// The [Widget] whose constrants will be set.
  final Widget child;

  /// Constructor.
  ChildConstraintsChanger({this.constraintsModel, this.child});

  @override
  _ChildConstraintsChangerState createState() =>
      new _ChildConstraintsChangerState();
}

class _ChildConstraintsChangerState extends State<ChildConstraintsChanger> {
  final GlobalKey _containerKey = new GlobalKey();
  List<BoxConstraints> _constraints;
  int _currentConstraintIndex = 0;

  @override
  void initState() {
    super.initState();
    _constraints = config.constraintsModel.constraints;
    config.constraintsModel.addListener(_onChange);
  }

  @override
  void dispose() {
    config.constraintsModel.removeListener(_onChange);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => new Container(
        foregroundDecoration: new RoundedCornerDecoration(
          radius: _kInnerBezelRadius,
          color: Colors.black,
        ),
        child: (_constraints?.isEmpty ?? true) ||
                (_constraints.length == 1 &&
                    _constraints[0] == const BoxConstraints())
            ? config.child
            : new Stack(
                children: <Widget>[
                  _constrainedChild,
                  _constraintSwitchingButton,
                ],
              ),
      );

  Widget get _constrainedChild => new LayoutBuilder(
        builder: (BuildContext context, BoxConstraints parentConstraints) =>
            new Container(
              decoration:
                  new BoxDecoration(backgroundColor: new Color(0xFF404040)),
              child: new Center(
                child: new Container(
                  padding: _currentConstraint == const BoxConstraints()
                      ? null
                      : new EdgeInsets.only(
                          bottom: _currentConstraint.maxHeight >
                                  _currentConstraint.maxWidth
                              ? _kBezelExtension
                              : 0.0,
                          right: _currentConstraint.maxHeight >
                                  _currentConstraint.maxWidth
                              ? 0.0
                              : _kBezelExtension),
                  decoration: _currentConstraint == const BoxConstraints()
                      ? null
                      : new BoxDecoration(
                          backgroundColor: Colors.black,
                          border: new Border.all(
                              color: Colors.black, width: _kBezelMinimumWidth),
                          borderRadius:
                              new BorderRadius.circular(_kOuterBezelRadius),
                          boxShadow: kElevationToShadow[12],
                        ),
                  child: new AnimatedContainer(
                    key: _containerKey,
                    width: _currentConstraint == const BoxConstraints()
                        ? parentConstraints.maxWidth
                        : _currentConstraint.maxWidth,
                    height: _currentConstraint == const BoxConstraints()
                        ? parentConstraints.maxHeight
                        : _currentConstraint.maxHeight,
                    duration: const Duration(milliseconds: 500),
                    curve: Curves.fastOutSlowIn,
                    child: new ClipRect(
                      child: config.child,
                    ),
                  ),
                ),
              ),
            ),
      );

  Widget get _constraintSwitchingButton => new Positioned(
        right: 0.0,
        top: 0.0,
        width: 50.0,
        height: 50.0,
        child: new GestureDetector(
          behavior: HitTestBehavior.translucent,
          onTap: _switchConstraints,
        ),
      );

  BoxConstraints get _currentConstraint {
    if (_constraints == null || _constraints.isEmpty) {
      return new BoxConstraints();
    }
    return _constraints[_currentConstraintIndex % _constraints.length];
  }

  void _switchConstraints() => setState(() {
        _currentConstraintIndex++;
      });

  void _onChange() {
    setState(() {
      _constraints = config.constraintsModel.constraints;
    });
  }
}
