// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'colors.dart';

// TODO(fxb/72922): Define TextTheme for Ermine.
// ignore: avoid_classes_with_only_static_members
class ErmineTextStyles {
  static final ErmineTextStyles _instance = ErmineTextStyles._internal();
  factory ErmineTextStyles() => _instance;
  ErmineTextStyles._internal();

  static const TextStyle headline1 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 48,
      height: 1.17, // 56
      color: ErmineColors.grey100);

  static const TextStyle headline2 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 36,
      height: 1.33, // 48
      color: ErmineColors.grey100);

  static const TextStyle headline3 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 24,
      height: 1.17, // 28
      color: ErmineColors.grey100);

  static const TextStyle headline4 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 18,
      height: 1.56, // 28
      color: ErmineColors.grey100);

  static const TextStyle subtitle1 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 18,
      height: 1.22, // 22
      color: ErmineColors.grey100);

  static const TextStyle subtitle2 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 16,
      height: 1.5, // 24
      color: ErmineColors.grey100);

  static const TextStyle bodyText1 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 14,
      height: 1.43, // 20
      color: ErmineColors.grey100);

  static const TextStyle bodyText2 = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 14,
      height: 1.14, // 16
      color: ErmineColors.grey100);

  static const TextStyle caption = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 12,
      height: 2, // 24
      color: ErmineColors.grey100);

  static const TextStyle overline = TextStyle(
      fontFamily: 'RobotoMono',
      fontSize: 11,
      height: 1.27, // 14
      color: ErmineColors.grey100);
}
