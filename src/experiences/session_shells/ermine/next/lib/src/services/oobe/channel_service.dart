// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';
import 'package:internationalization/strings.dart';

/// Defines a service to manage system udpate channels.
class ChannelService {
  late final ValueChanged<bool> onConnected;

  final _control = ChannelControlProxy();

  ChannelService() {
    Incoming.fromSvcPath().connectToService(_control);
    _control.ctrl.whenBound.then((_) => onConnected(true));
    _control.ctrl.whenClosed.then((_) => onConnected(false));
  }

  /// Get the current update channel.
  Future<String> get currentChannel async {
    final name = await _control.getTarget();
    return _shortNames[name] ?? name;
  }

  /// Get the list of available update channels.
  Future<List<String>> get channels => _control.getTargetList();

  /// Change the current update channel.
  Future<void> setCurrentChannel(String channel) => _control.setTarget(channel);

  /// Returns the mapping of channel name to it's description.
  static final descriptions = <String, String>{
    'beta': Strings.oobeBetaChannelDesc,
    'devhost': Strings.oobeDevhostChannelDesc,
    'dogfood': Strings.oobeDogfoodChannelDesc,
    'stable': Strings.oobeStableChannelDesc,
    'test': Strings.oobeTestChannelDesc,
  };

  /// Returns the mapping of internal channel name to it's short name.
  static final _shortNames = <String, String>{
    '2gmrtg05aspff9bisjxsu46no.fuchsia-updates.googleusercontent.com': 'test',
    '4igty6t46noanfx782kp9ywyc.fuchsia-updates.googleusercontent.com':
        'dogfood',
    'b5cvjayvpm75pukjav4d4hurk.fuchsia-updates.googleusercontent.com': 'beta',
    '4x15snlqjzlsgunidd0q1hj8n.fuchsia-updates.googleusercontent.com': 'stable',
  };

  void dispose() {
    _control.ctrl.close();
  }
}
