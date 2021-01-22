// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for displaying channel.
class Channel extends UiSpec {
  // Localized strings.
  static String get _title => Strings.channel;

  _ChannelModel _model;

  Channel(ChannelControlProxy control) {
    _model = _ChannelModel(control: control, onChange: _onChange);
  }

  factory Channel.fromStartupContext(StartupContext context) {
    final control = ChannelControlProxy();
    context.incoming.connectToService(control);
    return Channel(control);
  }

  void _onChange() {
    spec = _specForChannel(_model.channelName);
  }

  // TODO(fxb/67842): Add ability to switch OTA channels.
  @override
  void update(Value value) async {}

  @override
  void dispose() {
    _model.dispose();
  }

  static Spec _specForChannel(String channelName) {
    if (channelName == null) {
      return UiSpec.nullSpec;
    }
    return Spec(title: _title, groups: [
      Group(title: _title, values: [
        Value.withText(TextValue(text: channelName)),
      ]),
    ]);
  }
}

class _ChannelModel {
  final ChannelControlProxy control;
  final VoidCallback onChange;

  String _channelName;

  _ChannelModel({this.control, this.onChange}) {
    control.getTarget().then((name) {
      _channelName = name;
      onChange?.call();
    });
  }

  void dispose() {
    control.ctrl.close();
  }

  String get channelName => _channelName;
  set channelName(String name) {
    _channelName = name;
    onChange?.call();
  }
}
