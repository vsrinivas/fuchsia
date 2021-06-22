// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_memory/fidl_async.dart';
import 'package:fidl_fuchsia_session/fidl_async.dart' as session;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

import 'memory.dart' as memory;

const licenseUrl =
    'fuchsia-pkg://fuchsia.com/license_settings#meta/license_settings.cmx';
const feedbackUrl =
    'fuchsia-pkg://fuchsia.com/feedback_settings#meta/feedback_settings.cmx';

// ignore_for_file: prefer_constructors_over_static_methods

/// Defines a [UiSpec] for displaying system information.
class SystemInformation extends UiSpec {
  // Localized strings.
  static String get _title => Strings.systemInformation;
  static String get _memory => Strings.memory;
  static String get _view => Strings.view;
  static String get _loading => Strings.loading;
  static String get _feedback => Strings.feedback;
  static String get _openSource => Strings.openSource;
  static String get _license => Strings.license;

  // Icon for system information title.
  static IconValue get _icon =>
      IconValue(codePoint: Icons.info_outlined.codePoint);

  // Action to launch license.
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
        final index = value.text!.action ^ QuickAction.submit.$value;
        if (index == 1) {
          await launchUrl(feedbackUrl);
        }
        if (index == 2) {
          await launchUrl(licenseUrl);
        }
        spec = await _specForSystemInformation();
      }
    }
  }

  @override
  void dispose() {
    memoryModel.dispose();
  }

  Future<void> launchUrl(String url) async {
    final proxy = session.ElementManagerProxy();
    final elementController = session.ElementControllerProxy();

    final incoming = Incoming.fromSvcPath()..connectToService(proxy);

    final spec = session.ElementSpec(componentUrl: url);

    await proxy
        .proposeElement(spec, elementController.ctrl.request())
        .catchError((err) {
      log.shout('$err: Failed to propose element <$url>');
    });

    proxy.ctrl.close();
    await incoming.close();
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
                text: '${_view.toUpperCase()}',
                action: QuickAction.submit.$value | 1,
              ),
              TextValue(
                text: '${_openSource.toUpperCase()} ${_license.toUpperCase()}',
              ),
              TextValue(
                text: '${_view.toUpperCase()}',
                action: QuickAction.submit.$value | 2,
              )
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
