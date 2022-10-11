// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'colors.dart';

const _kDefaultIconColor = ErmineColors.grey100;
const _kDefaultIconSize = 24.0;

/// Ermine's system icon sets
///
/// To have the icons properly loaded in your component, follow the steps below:
/// 1. Copy ErmineIcons-Regular.ttf in fonts/ directory in this library and
/// paste into fonts/ of your component.
/// 2. Include the following in your pubspec.yaml of your component.
///
/// flutter:
///    fonts:
///    - family:  ErmineIcons
///      fonts:
///       - asset: fonts/ErmineIcons.ttf
///
/// Those steps are neccessary until we have a way to pipe assets from the
/// library to the package (fxb/73369)
class ErmineIcons extends Icon {
  const ErmineIcons._(
    IconData? icon, {
    Color? color = _kDefaultIconColor,
    double? size = _kDefaultIconSize,
    String? semanticLabel,
    TextDirection? textDirection,
    Key? key,
  }) : super(icon,
            color: color,
            size: size,
            semanticLabel: semanticLabel,
            textDirection: textDirection,
            key: key);

  static final add = ErmineIcons._(Icons.add);
  static final back = ErmineIcons._(Icons.arrow_back);
  static final bluetooth = ErmineIcons._(Icons.bluetooth);
  static final brightness = ErmineIcons._(Icons.brightness_5_outlined);
  static final channel = ErmineIcons._(Icons.cloud_queue);
  static final close = ErmineIcons._(Icons.clear);
  static final fuchsiaLogo = ErmineIcons._(IconData(
    0xe800,
    fontFamily: 'ErmineIcons',
    fontPackage: null,
  ));
  static final fullscreen = ErmineIcons._(Icons.fullscreen);
  static final info = ErmineIcons._(Icons.info_outline);
  static final keyboardInput = ErmineIcons._(Icons.keyboard_outlined);
  static final time = ErmineIcons._(Icons.access_time);
  static final volume = ErmineIcons._(Icons.volume_up_outlined);
  static final wifi = ErmineIcons._(Icons.wifi);

  ErmineIcons copyWith({
    Color? color,
    double? size,
    String? semanticLabel,
    TextDirection? textDirection,
    Key? key,
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
