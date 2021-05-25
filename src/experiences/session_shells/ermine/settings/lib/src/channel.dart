// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for displaying channel.
class Channel extends UiSpec {
  // Localized strings.
  static String get _title => Strings.channel;

  // Icon for channel title.
  static IconValue get _icon =>
      IconValue(codePoint: Icons.cloud_outlined.codePoint);

  // Action to change channel.
  static int changeAction = QuickAction.details.$value;

  late _ChannelModel model;

  Channel(ChannelControlProxy control) {
    model = _ChannelModel(control: control, onChange: _onChange);
  }

  factory Channel.withSvcPath() {
    final control = ChannelControlProxy();
    Incoming.fromSvcPath().connectToService(control);
    return Channel(control);
  }

  void _onChange() async {
    spec = await _specForChannel(model);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button &&
        value.button!.action == QuickAction.cancel.$value) {
      spec = await _specForChannel(model);
    } else if (value.$tag == ValueTag.text && value.text!.action > 0) {
      if (value.text!.action == changeAction) {
        spec = await _specForChannel(model, changeAction);
      } else {
        final index = value.text!.action ^ QuickAction.submit.$value;
        model.channel = model.channels[index];
        spec = await _specForChannel(model);
      }
    }
  }

  @override
  void dispose() {
    model.dispose();
  }

  Future<Spec> _specForChannel(_ChannelModel model, [int action = 0]) async {
    if (action == 0 || action & QuickAction.cancel.$value > 0) {
      return Spec(title: _title, groups: [
        Group(title: _title, icon: _icon, values: [
          Value.withText(TextValue(
            text: model.channel,
            action: changeAction,
          )),
          Value.withIcon(IconValue(
            codePoint: Icons.arrow_right.codePoint,
            action: changeAction,
          )),
        ]),
      ]);
    } else if (action == changeAction) {
      var channels = model.channels;
      final values = List<TextValue>.generate(
          channels.length,
          (index) => TextValue(
                text: channels[index],
                action: QuickAction.submit.$value | index,
              ));
      return Spec(title: _title, groups: [
        Group(title: 'Select Channel', values: [
          Value.withGrid(GridValue(
            columns: 1,
            values: values,
          )),
          Value.withButton(ButtonValue(
            label: 'close',
            action: QuickAction.cancel.$value,
          )),
        ]),
      ]);
    } else {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withText(TextValue(text: 'loading...')),
        ]),
      ]);
    }
  }
}

class _ChannelModel {
  final ChannelControlProxy control;
  final VoidCallback onChange;

  late String _channel;
  late List<String> _channels;

  _ChannelModel({required this.control, required this.onChange}) {
    loadCurrentChannel();
    loadTargetChannels();
  }

  void dispose() {
    control.ctrl.close();
  }

  String get channel => _channel;
  set channel(String name) {
    _channel = name;
    control.setTarget(name);
    onChange();
  }

  List<String> get channels => _channels;

  void loadCurrentChannel() {
    control.getTarget().then((name) {
      _channel = name;
      onChange();
    });
  }

  void loadTargetChannels() {
    control.getTargetList().then((channels) {
      _channels = channels;
    });
  }
}
