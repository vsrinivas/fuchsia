// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

import 'src/license.dart';

const _kErmineColor100 = Color(0xFFE5E5E5);
const _kErmineColor200 = Color(0xFFBDBDBD);
const _kErmineColor400 = Color(0xFF0C0C0C);

const _kTextStyle = TextStyle(color: _kErmineColor400, fontSize: 14.0);

/// Main entry point to the license settings module
void main() async {
  setupLogger(name: 'license_settings');

  runApp(
    MaterialApp(
      title: 'Open Source License',
      scrollBehavior: MaterialScrollBehavior().copyWith(
        dragDevices: {PointerDeviceKind.mouse, PointerDeviceKind.touch},
      ),
      theme: ThemeData(
        fontFamily: 'RobotoMono',
        textSelectionTheme: TextSelectionThemeData(
          selectionColor: _kErmineColor200,
          cursorColor: _kErmineColor400,
          selectionHandleColor: _kErmineColor400,
        ),
        hintColor: _kErmineColor400,
        primaryColor: _kErmineColor100,
        canvasColor: _kErmineColor100,
        highlightColor: _kErmineColor400,
        textTheme: TextTheme(
          bodyText2: _kTextStyle,
          subtitle1: _kTextStyle,
        ),
      ),
      home: Scaffold(
        body: Column(
          children: [
            Expanded(
              child: SingleChildScrollView(
                scrollDirection: Axis.vertical,
                child: License(),
              ),
            ),
          ],
        ),
      ),
    ),
  );
}
