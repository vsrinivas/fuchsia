// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing cpu metrics (mock data).
class Cpu extends UiSpec {
  static const _title = 'Cpu';

  _CpuModel _model;

  Cpu() : super(null) {
    _model = _CpuModel(onChange: _onChange);
  }

  factory Cpu.fromStartupContext() {
    return Cpu();
  }

  void _onChange() {
    spec = _specForCpu(_model._cpu);
  }

  @override
  void update(Value value) async {
    _model.cpu = value.progress.value;
  }

  @override
  void dispose() {
    _model.dispose();
  }

  static Spec _specForCpu(double value) {
    String valueString = (value * 100).toStringAsPrecision(3);
    return Spec(groups: [
      Group(title: _title, values: [
        Value.withGraph(GraphValue(value: value, step: 3)),
        Value.withText(TextValue(text: '$valueString%')),
      ]),
    ]);
  }
}

class _CpuModel {
  static const _checkCpuDuration = Duration(seconds: 1);

  final VoidCallback onChange;

  double _cpu;
  StreamSubscription _cpuSubscription;

  _CpuModel({this.onChange}) {
    _listen();
  }

  void dispose() {
    _cpuSubscription.cancel();
  }

  double get cpu => _cpu;
  set cpu(double value) {
    _cpu = value;
    onChange?.call();
  }

  void _listen() async {
    _cpuSubscription = Stream.periodic(_checkCpuDuration).listen((_) async {
      final mockData = Random().nextDouble();
      if (mockData != _cpu) {
        _cpu = mockData;
        onChange?.call();
      }
    });
  }
}
