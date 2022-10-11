// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../../visual_languages/colors.dart';
import '../../visual_languages/text_styles.dart';

/// Colors
const kNormalColor = ErmineColors.grey100;
const kNormalInverseColor = ErmineColors.grey600;
const kHoverColor = ErmineColors.fuchsia100;
const kDisabledColor = ErmineColors.grey300;

// Text Styles
const kLabelLargeTextStyle = ErmineTextStyles.headline3;
const kLabelMediumTextStyle = ErmineTextStyles.subtitle1;
const kLabelSmallTextStyle = ErmineTextStyles.bodyText2;

// Spacings & Sizings
const kMinSizeSmall = Size(24, 24);
const kMinSizeMedium = Size(30, 30);
const kMinSizeLarge = Size(36, 36);
const kButtonMargins = EdgeInsets.symmetric(horizontal: 8, vertical: 4);

// Others
const _kBorderWidth = 1.0;
const kNormalBorder = BorderSide(width: _kBorderWidth, color: kNormalColor);
const kHoveredBorder = BorderSide(width: _kBorderWidth, color: kHoverColor);
const kDisabledBorder = BorderSide(width: _kBorderWidth, color: kDisabledColor);
final kBorderCorner = RoundedRectangleBorder(
  borderRadius: BorderRadius.circular(4),
);
