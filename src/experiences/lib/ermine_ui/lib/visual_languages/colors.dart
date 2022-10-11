// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

// ignore: avoid_classes_with_only_static_members
class ErmineColors {
  static final ErmineColors _instance = ErmineColors._internal();
  factory ErmineColors() => _instance;
  ErmineColors._internal();

  static const Color white = Color(0xFFFFFFFF);
  static const Color black = Color(0xFF000000);

  static const Color grey600 = Color(0xFF0C0C0C);
  static const Color grey500 = Color(0xFF282828);
  static const Color grey400 = Color(0xFF595959);
  static const Color grey300 = Color(0xFF7D7D7D);
  static const Color grey200 = Color(0xFFBDBDBD);
  static const Color grey100 = Color(0xFFE5E5E5);

  static const Color fuchsia500 = Color(0xFF710074);
  static const Color fuchsia400 = Color(0xFFAC05B0);
  static const Color fuchsia300 = Color(0xFFE20AE7);
  static const Color fuchsia200 = Color(0xFFFD88FF);
  static const Color fuchsia100 = Color(0xFFFEC0FF);
}
