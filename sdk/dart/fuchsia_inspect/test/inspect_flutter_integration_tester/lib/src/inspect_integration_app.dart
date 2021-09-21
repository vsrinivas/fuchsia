// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';

/// A Flutter app that tests the [Inspect] API.
class InspectIntegrationApp extends StatelessWidget {
  /// Call InspectIntegrationApp.stateBloc.updateValue('new state') to display and
  /// key-publish 'new state'.
  static final StateBloc stateBloc = StateBloc();

  const InspectIntegrationApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Inspect Integration tester',
      theme: ThemeData(
        primarySwatch: Colors.blue,
      ),
      home: _InspectHomePage(title: 'Hello Inspect!'),
    );
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

class _InspectHomePage extends StatefulWidget {
  final String title;

  const _InspectHomePage({Key key, this.title}) : super(key: key);

  @override
  _InspectHomePageState createState() => _InspectHomePageState();
}

class _InspectHomePageState extends State<_InspectHomePage> {
  _InspectHomePageState();

  void config4k() {
    try {
      Inspect.configure(vmoSizeBytes: 4096);
      InspectIntegrationApp.stateBloc.updateValue('VMO set to 4k');
      // ignore: avoid_catches_without_on_clauses
    } catch (_) {
      InspectIntegrationApp.stateBloc.updateValue('ERROR setting 4k');
    }
  }

  void config16k() {
    try {
      Inspect.configure(vmoSizeBytes: 16384);
      InspectIntegrationApp.stateBloc.updateValue('VMO set to 16k');
      // ignore: avoid_catches_without_on_clauses
    } catch (_) {
      InspectIntegrationApp.stateBloc.updateValue('ERROR setting 16k');
    }
  }

  void testInspectInstance() {
    try {
      var inspect = Inspect();
      if (Inspect() != inspect) {
        throw (AssertionError('Inspect is not a singleton/factory'));
      }
      var root = inspect.root;
      if (root != inspect.root) {
        throw (AssertionError('root is not consistent'));
      }
      InspectIntegrationApp.stateBloc.updateValue('Inspect is correct');
      // ignore: avoid_catches_without_on_clauses
    } catch (_) {
      InspectIntegrationApp.stateBloc.updateValue('ERROR checking inspect');
    }
  }

  StreamBuilder<String> buildProgramStateWidget() {
    var stateBloc = InspectIntegrationApp.stateBloc;
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
      backgroundColor: Colors.white,
      body: Center(
          child: Column(children: [
        buildProgramStateWidget(),
      ])),
      persistentFooterButtons: <Widget>[
        TextButton(
          onPressed: config4k,
          child: Text('Config 4k'),
        ),
        TextButton(
          onPressed: config16k,
          child: Text('Config 16k'),
        ),
        TextButton(
          onPressed: testInspectInstance,
          child: Text('Test Inspect Instance'),
        ),
      ],
    );
  }
}
