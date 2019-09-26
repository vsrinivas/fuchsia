// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:quickui/uistream.dart';
import 'package:settings/settings.dart';
import 'package:fuchsia_services/services.dart';

import '../utils/utils.dart';

class StatusModel implements Inspectable {
  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'status');
  UiStream brightness;
  UiStream memory;
  StartupContext startupContext;

  StatusModel({this.startupContext}) {
    brightness = UiStream(Brightness.fromStartupContext(startupContext));
    memory = UiStream(Memory.fromStartupContext(startupContext));
  }

  factory StatusModel.fromStartupContext(StartupContext startupContext) {
    return StatusModel(startupContext: startupContext);
  }

  void dispose() {
    brightness.dispose();
    memory.dispose();
  }

  @override
  void onInspect(Node node) {
    if (key.currentContext != null) {
      final rect = rectFromGlobalKey(key);
      node
          .stringProperty('rect')
          .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');
    } else {
      node.delete();
    }
  }
}
