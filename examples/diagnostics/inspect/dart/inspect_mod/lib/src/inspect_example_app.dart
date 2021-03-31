// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer' show Timeline;
import 'dart:typed_data';

import 'package:async/async.dart';
import 'package:flutter/material.dart' hide Intent;
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_logger/logger.dart';

/// A Flutter app that demonstrates usage of the [Inspect] API.
class InspectExampleApp extends StatelessWidget {
  /// Call InspectExampleApp.stateBloc.updateValue('new state') to display and
  /// key-publish 'new state'.
  static final StateBloc stateBloc = StateBloc();

  static const _appColor = Colors.blue;

  final inspect.Node _inspectNode;

  InspectExampleApp(this._inspectNode) {
    _initProperties();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Inspect Example',
      theme: ThemeData(
        primarySwatch: _appColor,
      ),
      home: _InspectHomePage(
          title: 'Hello Inspect!',
          inspectNode: _inspectNode.child('home-page')),
    );
  }

  /// Initializes the [Inspect] properties for this widget.
  void _initProperties() {
    _inspectNode.stringProperty('greeting').setValue('Hello World');
    _inspectNode.doubleProperty('double down')
      ..setValue(1.23)
      ..add(2);
    _inspectNode.intProperty('interesting')
      ..setValue(123)
      ..subtract(5);
    _inspectNode
        .byteDataProperty('bytes')
        .setValue(ByteData(4)..setUint32(0, 0x01020304));
  }
}

/// The [StateBloc] provides actions and streams associated with
/// the agent that displays state on-screen and exports state keys for test.
class StateBloc {
  final _valueController = StreamController<String>.broadcast();
  String _lastKnownValue = 'Program has started';

  Stream<String> get valueStream => _valueController.stream;
  String get currentValue => _lastKnownValue;

  void updateValue(String newState) {
    _lastKnownValue = newState;
    _valueController.add(newState);
  }

  void dispose() {
    _valueController.close();
  }
}

class _AnswerFinder {
  static final _funnel = StreamController<int>();
  static final _faucet = StreamQueue<int>(_funnel.stream);

  Future<int> getTheAnswer() async {
    return await _faucet.next;
  }

  void takeAHint(int n) async {
    _funnel.add(n);
  }
}

class _InspectHomePage extends StatefulWidget {
  final String title;
  final inspect.Node inspectNode;

  _InspectHomePage({Key key, this.title, this.inspectNode}) : super(key: key) {
    inspectNode.stringProperty('title').setValue(title);
  }

  @override
  _InspectHomePageState createState() => _InspectHomePageState(inspectNode);
}

class _InspectHomePageState extends State<_InspectHomePage> {
  /// Possible background colors.
  static const _colors = [
    Colors.white,
    Colors.lime,
    Colors.orange,
  ];

  // Helpers to demo tree building and deletion
  final inspect.Node _inspectNode;
  inspect.Node _subtree;
  int _id = 0;

  // Helpers to demo auto-deletion lifecycle
  final _answerFinder = _AnswerFinder();
  int _nextHint = 40;
  String _answer = 'No answer requested yet';

  /// A property that tracks [_counter].
  final inspect.IntProperty _counterProperty;

  inspect.StringProperty _backgroundProperty;

  int _counter = 0;
  int _colorIndex = 0;

  _InspectHomePageState(this._inspectNode)
      : _counterProperty = _inspectNode.intProperty('counter') {
    _backgroundProperty = _inspectNode.stringProperty('background-color')
      ..setValue('$_backgroundColor');
  }

  Color get _backgroundColor => _colors[_colorIndex];

  void _incrementCounter() {
    setState(() {
      _counter++;

      // Note: an alternate approach that is also valid is to set the property
      // to the new value:
      //
      //     _counterProperty.setValue(_counter);
      Timeline.timeSync('Inc counter', () {
        _counterProperty.add(1);
      });
    });
    InspectExampleApp.stateBloc.updateValue('Counter was incremented');
  }

  void _decrementCounter() {
    setState(() {
      _counter--;
      _counterProperty.subtract(1);
    });
    InspectExampleApp.stateBloc.updateValue('Counter was decremented');
  }

  /// Increments through the possible [_colors].
  ///
  /// If we've reached the end, start over at the beginning.
  void _changeBackground() {
    setState(() {
      _colorIndex++;

      _colorIndex %= _colors.length;

      _backgroundProperty?.setValue('$_backgroundColor');
    });
    InspectExampleApp.stateBloc.updateValue('Color was changed');
  }

  void _makeTree() {
    // Make a long tree name on purpose, to see how far we can push the naming.
    _subtree = _inspectNode.child(
        'I think that I shall never see01234567890123456789012345678901234567890')
      ..intProperty('int$_id').setValue(_id++);
    InspectExampleApp.stateBloc.updateValue('Tree was made');
  }

  void _addToTree() {
    _subtree?.intProperty('int$_id')?.setValue(_id++);
    InspectExampleApp.stateBloc.updateValue('Tree was grown');
  }

  void _deleteTree() {
    _subtree?.delete();
    InspectExampleApp.stateBloc.updateValue('Tree was deleted');
  }

  void _giveHint() {
    _answerFinder.takeAHint(_nextHint++);
    InspectExampleApp.stateBloc.updateValue('Gave a hint');
  }

  void _showAnswer() {
    var answerFuture = _answerFinder.getTheAnswer();
    var wait = _inspectNode.stringProperty('waiting')..setValue('for a hint');
    answerFuture.whenComplete(wait.delete);
    setState(() {
      _answer = 'Waiting for answer';
    });
    InspectExampleApp.stateBloc.updateValue('Waiting for answer');
    answerFuture.then((answer) {
      setState(() {
        _answer = 'Answer is: $answer';
      });
      InspectExampleApp.stateBloc.updateValue('Displayed answer');
    }).catchError(
      (e, s) {
        log.info(' * * Hi2 from inspect_mod');
        setState(() {
          _answer = 'Something went wrong getting answer:\n$e\n$s';
        });
      },
    );
  }

  StreamBuilder<String> buildProgramStateWidget() {
    var stateBloc = InspectExampleApp.stateBloc;
    return StreamBuilder<String>(
        stream: stateBloc.valueStream,
        initialData: stateBloc.currentValue,
        builder: (BuildContext context, AsyncSnapshot<String> snapshot) {
          if (snapshot.data == '') {
            // don't display anything
            return Offstage();
          } else {
            return Container(
              alignment: Alignment.center,
              child: Text('State: ${snapshot.data}',
                  style: TextStyle(fontSize: 34.0)),
              key: Key(snapshot.data),
            );
          }
        });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(
          widget.title,
        ),
      ),
      backgroundColor: _backgroundColor,
      body: Center(
          child: Column(children: [
        buildProgramStateWidget(),
        Text('Counter: $_counter', style: TextStyle(fontSize: 48.0)),
        Text('$_answer', style: TextStyle(fontSize: 48.0)),
      ])),
      persistentFooterButtons: <Widget>[
        FlatButton(
          onPressed: _giveHint,
          child: Text('Give hint'),
        ),
        FlatButton(
          onPressed: _showAnswer,
          child: Text('Get answer'),
        ),
        FlatButton(
          onPressed: _changeBackground,
          child: Text('Change color'),
        ),
        FlatButton(
          onPressed: _makeTree,
          child: Text('Make tree'),
        ),
        FlatButton(
          onPressed: _addToTree,
          child: Text('Grow tree'),
        ),
        FlatButton(
          onPressed: _deleteTree,
          child: Text('Delete tree'),
        ),
        FlatButton(
          onPressed: _incrementCounter,
          child: Text('Increment counter'),
        ),
        FlatButton(
          onPressed: _decrementCounter,
          child: Text('Decrement counter'),
        ),
      ],
    );
  }
}
