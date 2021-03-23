// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'colors.dart';

const _kDefaultIconColor = ErmineColors.grey100;
const _kDefaultIconSize = 24.0;

/// Ermine's system icon sets
class ErmineIcons extends Icon {
  const ErmineIcons._(
    IconData icon, {
    Color color = _kDefaultIconColor,
    double size = _kDefaultIconSize,
    String semanticLabel,
    TextDirection textDirection,
    Key key,
  }) : super(icon,
            color: color,
            size: size,
            semanticLabel: semanticLabel,
            textDirection: textDirection,
            key: key);

  // TODO(fxb/72868): Add more icons currently in use or will be used in Ermine.
  static final add = ErmineIcons._(Icons.add);
  static final back = ErmineIcons._(Icons.arrow_back);
  static final close = ErmineIcons._(Icons.clear);

  ErmineIcons copyWith({
    Color color,
    double size,
    String semanticLabel,
    TextDirection textDirection,
    Key key,
  }) =>
      ErmineIcons._(
        super.icon,
        color: color ?? super.color,
        size: size ?? super.size,
        semanticLabel: semanticLabel ?? super.semanticLabel,
        textDirection: textDirection ?? super.textDirection,
        key: key ?? super.key,
      );
}
