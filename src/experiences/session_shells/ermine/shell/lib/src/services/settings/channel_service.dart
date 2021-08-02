// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to control channel in QuickSettings.
class ChannelService extends TaskService {
  late final VoidCallback onChanged;
  late final ValueChanged<bool> onConnected;

  var _control = ChannelControlProxy();

  late String _currentChannel;
  late List<String> _availableChannels;

  @override
  Future<void> start() async {
    Incoming.fromSvcPath().connectToService(_control);

    await _control.getTarget().then((name) {
      _currentChannel = _shortNames[name] ?? name;
      onChanged();
    });

    await _control.getTargetList().then((channels) {
      _availableChannels = channels;
    });
  }

  @override
  Future<void> stop() async {
    dispose();
  }

  String get currentChannel => _shortNames[_currentChannel] ?? _currentChannel;

  bool get optedIntoUpdates =>
      ((_shortNames[_currentChannel] ?? _currentChannel) != 'devhost') &&
      ((_shortNames[_currentChannel] ?? _currentChannel) != 'fuchsia.com');

  List<String> get channels => _availableChannels;

  /// Returns the mapping of internal channel name to it's short name.
  static final _shortNames = <String, String>{
    '2gmrtg05aspff9bisjxsu46no.fuchsia-updates.googleusercontent.com': 'test',
    '4igty6t46noanfx782kp9ywyc.fuchsia-updates.googleusercontent.com':
        'dogfood',
    'b5cvjayvpm75pukjav4d4hurk.fuchsia-updates.googleusercontent.com': 'beta',
    '4x15snlqjzlsgunidd0q1hj8n.fuchsia-updates.googleusercontent.com': 'stable',
  };

  @override
  void dispose() {
    _control.ctrl.close();
    _control = ChannelControlProxy();
  }
}
