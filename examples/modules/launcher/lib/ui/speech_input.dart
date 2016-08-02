// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:mojo_services/speech_recognizer/speech_recognizer.mojom.dart'
    as speech;

import 'circle_painter.dart';
import 'rk4_spring_simulation.dart';
import 'ticking_simulation.dart';

const double _kExtentSimulationTension = 150.0;
const double _kExtentSimulationFriction = 25.0;
const RK4SpringDescription kExtentSimulationDesc = const RK4SpringDescription(
    tension: _kExtentSimulationTension, friction: _kExtentSimulationFriction);

const double _kButtonCircleDiameter = 14.0;
const double _kButtonCircleMargin = 13.0;
const double _kSoundLevelRadius = 128.0;

typedef void OnSpeechRecognized(String speech);
typedef void OnListeningChanged(bool listening);

class SpeechInput extends StatefulWidget {
  final OnSpeechRecognized onSpeechRecognized;
  final OnListeningChanged onListeningChanged;

  SpeechInput({Key key, this.onSpeechRecognized, this.onListeningChanged})
      : super(key: key);

  @override
  SpeechInputState createState() => new SpeechInputState();
}

class SpeechInputState extends State<SpeechInput>
    implements speech.SpeechRecognizerListener {
  speech.SpeechRecognizerServiceProxy _speechRecognizerService;
  TickingSimulation _tickingSimulation;
  _SpeechRecognizerListener _currentListener;
  bool _isListening = false;
  bool _pipeIsBad = false;

  SpeechInputState() {
    _speechRecognizerService = shell.connectToApplicationService(
        'mojo:speech_recognizer',
        speech.SpeechRecognizerService.connectToService);
    _speechRecognizerService.ctrl.errorFuture.then((_) {
      _pipeIsBad = true;
      _tickingSimulation.target = 0.0;
      _listening = false;
    });
    _tickingSimulation = new TickingSimulation(
        simulation: new RK4SpringSimulation(
            initValue: 0.0, desc: kExtentSimulationDesc),
        onTick: _markNeedsBuild);
  }

  void _markNeedsBuild() {
    setState(() {});
  }

  @override
  Widget build(_) {
    return new Stack(children: <Widget>[
      new Positioned(
          left: 0.0,
          right: 0.0,
          top: 0.0,
          bottom: 0.0,
          child: new CustomPaint(painter: new CirclePainter(
              bottomOffset: _kButtonCircleMargin + _kButtonCircleDiameter / 2.0,
              radius: _tickingSimulation.value))),
      new Align(
          alignment: const FractionalOffset(0.5, 1.0),
          child: new Container(
              margin: new EdgeInsets.all(_kButtonCircleMargin),
              width: _kButtonCircleDiameter,
              height: _kButtonCircleDiameter,
              decoration: new BoxDecoration(
                  backgroundColor: new Color(0xFFFFFFFF),
                  shape: BoxShape.circle))),
      new Align(
          alignment: const FractionalOffset(0.5, 1.0),
          child: new Listener(
              behavior: HitTestBehavior.opaque,
              onPointerDown: (_) {
                startListening();
              },
              onPointerUp: (_) {
                stopListening();
              },
              child: new Container(width: 128.0, height: 128.0)))
    ]);
  }

  bool get isListening => _isListening;

  void startListening() {
    if (!isListening && !_pipeIsBad) {
      _currentListener = new _SpeechRecognizerListener(this);
      _listening = true;
      _speechRecognizerService.listen(_currentListener);
    }
  }

  void stopListening() {
    if (isListening && !_pipeIsBad) {
      _speechRecognizerService.stopListening();
    }
  }

  @override
  void onRecognizerError(speech.Error errorCode) {
    setState(() {
      _tickingSimulation.target = 0.0;
      _listening = false;
    });
  }

  @override
  void onResults(List<speech.UtteranceCandidate> candidates, bool complete) {
    if (complete) {
      _listening = false;
      _tickingSimulation.target = 0.0;
    }
    if (candidates.isNotEmpty) {
      if (config.onSpeechRecognized != null) {
        config.onSpeechRecognized(candidates[0].text);
      }
    }
  }

  @override
  void onSoundLevelChanged(double rmsDb) {
    setState(() {
      _tickingSimulation.target =
          _kSoundLevelRadius * (math.min(math.max(rmsDb, 0.0), 10.0) / 10.0);
    });
  }

  set _listening(bool listening) {
    setState(() {
      _isListening = listening;
    });
    if (config.onListeningChanged != null) {
      config.onListeningChanged(_isListening);
    }
  }
}

class _SpeechRecognizerListener extends speech.SpeechRecognizerListenerStub {
  _SpeechRecognizerListener(speech.SpeechRecognizerListener listener)
      : super.unbound() {
    this.impl = listener;
  }
}
