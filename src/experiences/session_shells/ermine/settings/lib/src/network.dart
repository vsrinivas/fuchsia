// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing network metrics (mock data).
class Network extends UiSpec {
  static const _title = 'Network';

  final String _network = 'Wireless_Network';

  Network() {
    spec = _specForNetwork(_network);
  }

  factory Network.fromStartupContext() {
    return Network();
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {}

  static Spec _specForNetwork(String value) {
    return Spec(groups: [
      Group(title: _title, values: [
        Value.withText(TextValue(text: value)),
        Value.withIcon(IconValue(codePoint: Icons.wifi.codePoint)),
      ]),
    ]);
  }
}
