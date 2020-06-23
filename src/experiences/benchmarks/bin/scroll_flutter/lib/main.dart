// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:developer';
import 'dart:ui' as ui;

import 'package:args/args.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:lib.widgets/utils.dart';

void main(List<String> args) {
  final argParser = ArgParser()
    ..addOption('sampling-offset-ms',
        abbr: 'o',
        valueHelp: 'n',
        help: 'Sample touch events with offset.',
        defaultsTo: '-38')
    ..addFlag('resample', defaultsTo: true)
    ..addFlag('help',
        abbr: 'h', help: 'Displays usage information.', negatable: false);

  final argResults = argParser.parse(args);
  if (argResults['help']) {
    print(argParser.usage);
    return;
  }

  runApp(Scroll.fromArgResults(argResults));
}

@immutable
class Scroll extends StatefulWidget {
  final Duration samplingOffset;
  final bool resample;

  const Scroll(this.samplingOffset, {this.resample});

  factory Scroll.fromArgResults(ArgResults parsed) {
    return Scroll(
        Duration(milliseconds: int.parse(parsed['sampling-offset-ms'])),
        resample: parsed['resample']);
  }

  @override
  State<StatefulWidget> createState() =>
      ScrollState(samplingOffset, resample: resample);
}

class ScrollState extends State<Scroll> {
  final Duration samplingOffset;
  final bool resample;

  ScrollController _controller;
  SchedulerBinding _scheduler;
  var _frameCallbackScheduled = false;
  var _sampleTime = Duration();
  var _lastPointerDataTimeStamp = Duration();
  ui.PointerDataPacketCallback _callback;
  final _resamplers = <int, PointerDataResampler>{};

  ScrollState(this.samplingOffset, {this.resample});

  void _onPointerDataPacket(ui.PointerDataPacket packet) {
    Timeline.timeSync('onPointerDataPacket', () {
      _lastPointerDataTimeStamp = packet.data.last.timeStamp;
      for (var data in packet.data) {
        if (resample && data.kind == ui.PointerDeviceKind.touch) {
          var resampler = _resamplers.putIfAbsent(
              data.device, () => PointerDataResampler());
          var dataArguments = <String, int>{
            'change': data.change.index,
            'physicalX': data.physicalX.toInt(),
            'physicalY': data.physicalY.toInt(),
          };
          Timeline.timeSync('addPointerData', () {
            resampler.addData(data);
          }, arguments: dataArguments);
          _dispatchPointerData();
        } else {
          _dispatchPointerDataPacket(ui.PointerDataPacket(data: [data]));
        }
      }
    });
  }

  void _dispatchPointerDataPacket(ui.PointerDataPacket packet) {
    Timeline.timeSync('dispatchPointerDataPacket', () {
      _callback(packet);
    });
  }

  void _dispatchPointerData() {
    for (var resampler in _resamplers.values) {
      final packets = resampler.sample(_sampleTime);
      if (packets.isNotEmpty) {
        for (var data in packets) {
          var dataArguments = <String, int>{
            'change': data.change.index,
            'physicalX': data.physicalX.toInt(),
            'physicalY': data.physicalY.toInt(),
            'physicalDeltaX': data.physicalDeltaX.toInt(),
            'physicalDeltaY': data.physicalDeltaY.toInt(),
          };
          Timeline.timeSync('dispatchPointerData', () {},
              arguments: dataArguments);
        }
        _dispatchPointerDataPacket(ui.PointerDataPacket(data: packets));
      }
      if (resampler.hasPendingData() || resampler.isTracked()) {
        _scheduleFrameCallback();
      }
    }
  }

  void _scheduleFrameCallback() {
    if (_frameCallbackScheduled) {
      return;
    }
    _frameCallbackScheduled = true;
    _scheduler.scheduleFrameCallback((_) {
      _frameCallbackScheduled = false;
      _sampleTime = _scheduler.currentSystemFrameTimeStamp + samplingOffset;
      var frameArguments = <String, int>{
        'frameTimeUs': _scheduler.currentSystemFrameTimeStamp.inMicroseconds,
        'lastDataTimeStampUs': _lastPointerDataTimeStamp.inMicroseconds,
        'timeStampMarginUs':
            (_lastPointerDataTimeStamp - _sampleTime).inMicroseconds
      };
      Timeline.timeSync('onFrameCallback', _dispatchPointerData,
          arguments: frameArguments);
    });
  }

  @override
  void initState() {
    _controller = ScrollController(initialScrollOffset: 8000.0);
    _scheduler = SchedulerBinding.instance;
    _callback = ui.window.onPointerDataPacket;
    ui.window.onPointerDataPacket = _onPointerDataPacket;
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return ConstrainedBox(
      constraints: BoxConstraints.expand(),
      child: Container(
        color: Colors.white,
        child: ListView(
          controller: _controller,
          children: <Widget>[
            Container(height: 8192.0),
            Center(
              child: Text(
                'Scroll Me!',
                style: TextStyle(
                  color: Colors.black,
                  fontWeight: FontWeight.bold,
                  fontSize: 32,
                ),
              ),
            ),
            Container(height: 8192.0),
          ],
        ),
      ),
    );
  }
}
