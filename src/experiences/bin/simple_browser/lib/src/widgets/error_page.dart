// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async' show Timer;
import 'dart:math' show Random;
import 'dart:ui' show lerpDouble;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show RawKeyDownEvent, RawKeyEvent;

const _kStartLength = 5;
const _kMaxCrumbs = 20;
const _kCrumbChanceToAppear = 0.05;
const _kKeyLabelToDirection = <String, Coord>{
  'w': Coord.up,
  'a': Coord.left,
  's': Coord.down,
  'd': Coord.right,
};
const _kInitialCoords = <Coord>[
  Coord(-2, 0),
  Coord(-1, 0),
  Coord(0, 0),
  Coord(1, 0),
  Coord(2, 0),
];

class ErrorPage extends StatefulWidget {
  @override
  _ErrorPageState createState() => _ErrorPageState();
}

class _ErrorPageState extends State<ErrorPage> {
  final _focusNode = FocusNode();
  final _direction = ValueNotifier<Coord?>(null);
  final _coords = <Coord>[];
  final _crumbCoords = <Coord>[];
  int _length = _kStartLength;
  Timer? _timer;
  Size? _screenSize;
  Offset? _screenCenter;
  bool _lost = false;
  final _random = Random();
  double rnd() => _random.nextDouble();

  @override
  void initState() {
    super.initState();
    if (WidgetsBinding.instance != null) {
      WidgetsBinding.instance!.addPostFrameCallback(
          (_) => FocusScope.of(context).requestFocus(_focusNode));
    }
    _reset();
  }

  void _reset() {
    _coords
      ..clear()
      ..addAll(_kInitialCoords);
    _crumbCoords.clear();
    _length = _kStartLength;
    _lost = false;
  }

  void _startTimer() {
    _timer?.cancel();
    _timer = Timer.periodic(Duration(milliseconds: 250), _onTimer);
  }

  @override
  void dispose() {
    _timer?.cancel();
    _focusNode.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => RawKeyboardListener(
        focusNode: FocusNode(),
        onKey: _onKey,
        child: GestureDetector(
          child: Container(
            color: Colors.transparent,
            child: LayoutBuilder(builder: (context, constraints) {
              _screenSize = constraints.biggest;
              _screenCenter = _screenSize!.center(Offset.zero);
              return Stack(
                children: [
                  ..._coords.asMap().map(_buildBody).values.toList(),
                  ..._crumbCoords.map(_buildCrumb).toList(),
                  Opacity(
                    opacity: 0,
                    child: TextField(
                      focusNode: _focusNode,
                      autofocus: true,
                    ),
                  ),
                ],
              );
            }),
          ),
          onTap: () => FocusScope.of(context).requestFocus(_focusNode),
        ),
      );

  void _onKey(RawKeyEvent value) {
    if (value.runtimeType == RawKeyDownEvent) {
      if (_lost) {
        _reset();
      }

      Coord? newDirection = _kKeyLabelToDirection[value.logicalKey.keyLabel];
      if (newDirection != null) {
        if (_coords[_coords.length - 2] - _coords.last != newDirection) {
          _direction.value = newDirection;
          _onTimer(null);
          _startTimer();
        }
      }
    }
  }

  String _textForIndex(int index) {
    if (index == 0) {
      return 'E';
    } else if (index == _coords.length - 2) {
      return 'O';
    } else {
      return 'R';
    }
  }

  Widget _buildCrumb(Coord coord) =>
      _buildSquare(squaresToScreen(coord), 'R', false);

  MapEntry<int, Widget> _buildBody(int index, Coord coord) => MapEntry(
      index, _buildSquare(squaresToScreen(coord), _textForIndex(index), true));

  Widget _buildSquare(Offset offset, String string, bool invert) => Positioned(
        left: offset.dx,
        top: offset.dy,
        child: Container(
          width: 16,
          height: 16,
          color: invert ? Colors.black : null,
          child: Text(
            string,
            textAlign: TextAlign.center,
            style: invert ? TextStyle(color: Colors.white) : null,
          ),
        ),
      );

  Coord screenToSquares(Offset screen) => Coord(
      ((screen.dx - _screenCenter!.dx) / 16).floor(),
      ((screen.dy - _screenCenter!.dy) / 16).floor());
  Offset squaresToScreen(Coord squares) =>
      Offset((squares.x - 0.5), (squares.y - 0.5)) * 16 + _screenCenter!;

  void _addCrumb() {
    Coord newCrumb = screenToSquares(Offset(
      lerpDouble(0, _screenSize!.width, rnd())!,
      lerpDouble(0, _screenSize!.height, rnd())!,
    ));

    // add if coordinate is currently free
    if (!_coords.contains(newCrumb) && !_crumbCoords.contains(newCrumb)) {
      _crumbCoords.add(newCrumb);
    }
  }

  void _onTimer(Timer? timer) {
    if (_direction.value == null) {
      return;
    }

    Coord newCoord = _coords.last + _direction.value!;

    // lost: eating own tail
    if (_coords.contains(newCoord)) {
      _lost = true;
      _timer?.cancel();
      return;
    }

    // lost: leaving the screen
    if (!(Offset.zero & _screenSize!).contains(squaresToScreen(newCoord))) {
      _lost = true;
      _timer?.cancel();
      return;
    }

    setState(() {
      _coords.add(newCoord);

      if (_coords.length > _length) {
        _coords.removeAt(0);
      }

      // yum
      if (_crumbCoords.remove(newCoord)) {
        _length++;
      }

      // more food
      if (_crumbCoords.length < _kMaxCrumbs && rnd() < _kCrumbChanceToAppear) {
        _addCrumb();
      }
    });
  }
}

class Coord {
  const Coord(this.x, this.y);
  final int x;
  final int y;

  static const Coord up = Coord(0, -1);
  static const Coord left = Coord(-1, 0);
  static const Coord down = Coord(0, 1);
  static const Coord right = Coord(1, 0);

  Coord operator -(Coord other) => Coord(x - other.x, y - other.y);
  Coord operator +(Coord other) => Coord(x + other.x, y + other.y);

  @override
  bool operator ==(dynamic other) {
    if (other is! Coord) {
      return false;
    }
    final Coord typedOther = other;
    return x == typedOther.x && y == typedOther.y;
  }

  @override
  int get hashCode => hashValues(x, y);
}
