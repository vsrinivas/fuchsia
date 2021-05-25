// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_memory/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

import 'memory.dart' as memory;

// ignore_for_file: prefer_constructors_over_static_methods

/// Defines a [UiSpec] for displaying system information.
class SystemInformation extends UiSpec {
  // Localized strings.
  static String get _title => Strings.systemInformation;
  static String get _memory => Strings.memory;
  static String get _view => Strings.view;
  static String get _loading => Strings.loading;
  static String get _feedback => Strings.feedback;
  static String get _please => Strings.please;
  static String get _visit => Strings.visit;

  // Icon for system information title.
  static IconValue get _icon =>
      IconValue(codePoint: Icons.info_outlined.codePoint);

  // Action to change channel.
  static int changeAction = QuickAction.details.$value;

  // Memory model and variables
  late memory.MemoryModel memoryModel;
  late String usedMemory;
  late String totalMemory;

  SystemInformation({required Monitor monitor, WatcherBinding? binding}) {
    memoryModel = memory.MemoryModel(
        monitor: monitor, binding: binding, onChange: _onChangeMemory);
    _onChange();
  }

  factory SystemInformation.withSvcPath() {
    final monitor = MonitorProxy();
    Incoming.fromSvcPath().connectToService(monitor);
    return SystemInformation(monitor: monitor);
  }

  void _onChange() async {
    spec = await _specForSystemInformation();
  }

  void _onChangeMemory() async {
    usedMemory = (memoryModel.memUsed).toStringAsPrecision(3);
    totalMemory = (memoryModel.memTotal).toStringAsPrecision(3);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button &&
        value.button!.action == QuickAction.cancel.$value) {
      spec = await _specForSystemInformation();
    } else if (value.$tag == ValueTag.text && value.text!.action > 0) {
      if (value.text!.action == changeAction) {
        spec = await _specForSystemInformation(changeAction);
      } else {
        spec = await _specForSystemInformation();
      }
    }
  }

  @override
  void dispose() {
    memoryModel.dispose();
  }

  Future<Spec> _specForSystemInformation([int action = 0]) async {
    if (action == 0 || action & QuickAction.cancel.$value > 0) {
      return Spec(title: _title, groups: [
        Group(title: _title, icon: _icon, values: [
          Value.withText(TextValue(
            text: _view,
            action: changeAction,
          )),
          Value.withIcon(IconValue(
            codePoint: Icons.arrow_right.codePoint,
            action: changeAction,
          )),
        ]),
      ]);
    } else if (action == changeAction) {
      return Spec(title: _title, groups: [
        Group(title: '', values: [
          Value.withGrid(GridValue(
            columns: 2,
            values: [
              TextValue(text: '${_memory.toUpperCase()}'),
              TextValue(text: '${usedMemory}GB / ${totalMemory}GB'),
              TextValue(text: '${_feedback.toUpperCase()}'),
              TextValue(
                  text:
                      '$_please ${_visit.toLowerCase()} https://fuchsia.dev/fuchsia-src/contribute/report-issue'),
            ],
          )),
        ]),
      ]);
    } else {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withText(TextValue(text: '$_loading...')),
        ]),
      ]);
    }
  }
}
