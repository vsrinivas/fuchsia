// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Helper widget extensions to help flatten widget trees.
extension WidgetExtension on Widget {
  Widget tooltip(String message) {
    return Tooltip(message: message, child: this);
  }

  Widget padding(EdgeInsetsGeometry value) {
    return Padding(padding: value, child: this);
  }
}
