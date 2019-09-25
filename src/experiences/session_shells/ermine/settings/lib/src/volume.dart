// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing volume metrics (mock data).
class Volume extends UiSpec {
  static const _title = 'Volume';

  final double _volume = .5;

  Volume() {
    spec = _specForVolume(_volume);
  }

  factory Volume.fromStartupContext() {
    return Volume();
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {}

  static Spec _specForVolume(double value) {
    return Spec(groups: [
      Group(title: _title, values: [
        Value.withIcon(IconValue(codePoint: Icons.volume_down.codePoint)),
        Value.withProgress(ProgressValue(value: value)),
        Value.withIcon(IconValue(codePoint: Icons.volume_up.codePoint)),
      ]),
    ]);
  }
}
