// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_memory/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing memory.
class Memory extends UiSpec {
  static String get _memory => Strings.memory;

  MemoryModel model;

  Memory({Monitor monitor, WatcherBinding binding}) {
    model = MemoryModel(
      monitor: monitor,
      binding: binding,
      onChange: _onChange,
    );
  }

  factory Memory.fromStartupContext(StartupContext startupContext) {
    final monitor = MonitorProxy();
    startupContext.incoming.connectToService(monitor);
    return Memory(monitor: monitor);
  }

  void _onChange() {
    spec = _specForMemory(model.memory, model.memUsed, model.memTotal);
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {
    model.dispose();
  }

  static Spec _specForMemory(double value, double used, double total) {
    String usedString = (used).toStringAsPrecision(3);
    String totalString = (total).toStringAsPrecision(3);
    return Spec(title: _memory, groups: [
      Group(title: _memory, values: [
        Value.withText(TextValue(text: '${usedString}GB / ${totalString}GB')),
      ]),
    ]);
  }
}

class MemoryModel {
  final VoidCallback onChange;
  final WatcherBinding _binding;
  double memUsed;
  double memTotal;
  double _memory;

  MemoryModel({
    @required this.onChange,
    WatcherBinding binding,
    Monitor monitor,
  }) : _binding = binding ?? WatcherBinding() {
    monitor.watch(_binding.wrap(_MonitorWatcherImpl(this)));
  }

  void dispose() {
    _binding.close();
  }

  double get memory => _memory;
  set memory(double value) {
    _memory = value;
    onChange?.call();
  }

  void updateMem(Stats stats) {
    memUsed = _bytesToGB(stats.totalBytes - stats.freeBytes);
    memTotal = _bytesToGB(stats.totalBytes);
    memory = memUsed / memTotal;
  }

  double _bytesToGB(int bytes) {
    return (bytes / pow(1024, 3));
  }
}

class _MonitorWatcherImpl extends Watcher {
  final MemoryModel memoryModel;
  _MonitorWatcherImpl(this.memoryModel);

  @override
  Future<void> onChange(Stats stats) async {
    memoryModel.updateMem(stats);
  }
}
