// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;

import 'package:args/args.dart';
import 'package:flutter/material.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_driver/driver_extension.dart';

void main([List<String> args = const []]) {
  final argParser = ArgParser()
    ..addOption('sampling-offset-ms',
        abbr: 'o',
        valueHelp: 'n',
        help: 'Sample touch events with offset.',
        defaultsTo: '-38')
    ..addFlag('resample', defaultsTo: true)
    ..addFlag('help',
        abbr: 'h', help: 'Displays usage information.', negatable: false)
    ..addFlag('flutter-driver',
        help: 'Enable the Flutter Driver extension.', defaultsTo: false);

  final argResults = argParser.parse(args);
  if (argResults['help']) {
    print(argParser.usage);
    return;
  }

  if (argResults['flutter-driver']) {
    enableFlutterDriverExtension();
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

  ScrollState(this.samplingOffset, {this.resample});

  @override
  void initState() {
    _controller = ScrollController(initialScrollOffset: 8000.0);
    GestureBinding.instance.resamplingEnabled = resample;
    GestureBinding.instance.samplingOffset = samplingOffset;
    debugPrintResamplingMargin = true;
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return Directionality(
      textDirection: ui.TextDirection.ltr,
      child: ConstrainedBox(
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
      ),
    );
  }
}
