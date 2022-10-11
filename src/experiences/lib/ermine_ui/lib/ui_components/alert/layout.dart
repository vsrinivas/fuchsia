// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../../visual_languages/colors.dart';
import '../../visual_languages/text_styles.dart';

/// Colors
const kBackgroundColor = ErmineColors.grey600;
const kBorderColor = ErmineColors.grey100;

/// Text Styles
const kHeaderTextStyle = ErmineTextStyles.caption;
final kTitleTextStyle =
    ErmineTextStyles.headline3.copyWith(fontWeight: FontWeight.bold);
const kDescriptionTextStyle = ErmineTextStyles.headline4;

/// Spacings & Sizings
const kWindowWidthMax = 816.0;
const kHeaderPaddings = EdgeInsets.fromLTRB(24, 4, 8, 4);
const kHeaderTextToIconGap = 24.0;
const kBodyPaddings = EdgeInsets.fromLTRB(24, 24, 24, 36);
const kTitleToDescriptionGap = 16.0;
const kDescriptionToCustomWidgetGap = 32.0;
const kButtonRowPaddings = EdgeInsets.fromLTRB(24, 0, 24, 32);
const kButtonToButtonGap = 16.0;
const kCloseIconSize = 16.0;
const kBorderThickness = 1.0;

/// Others
final kAlertShadow = BoxShadow(
  color: ErmineColors.grey500,
  spreadRadius: 0,
  blurRadius: 0,
  offset: Offset(16, 16),
);
