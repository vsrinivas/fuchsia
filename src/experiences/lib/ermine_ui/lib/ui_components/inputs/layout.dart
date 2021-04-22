// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../../visual_languages/colors.dart';
import '../../visual_languages/text_styles.dart';

// Colors
const kCursorColor = ErmineColors.grey100;
const kFieldBgColor = ErmineColors.grey500;
const kFieldTextColor = ErmineColors.grey100;

// Typography
const kFieldTextStyle = ErmineTextStyles.headline4;
final kLabelTextStyle =
    ErmineTextStyles.headline4.copyWith(fontWeight: FontWeight.w600);
final kHintTextStyle =
    ErmineTextStyles.headline4.copyWith(color: ErmineColors.grey300);

// Spaces & Sizes
const kLabelToFieldGap = 16.0;
const kCursorToHintGap = 8.0;
const kCursorWidth = 8.0;
const kCursorHeight = 16.0;
const kFieldPaddings = EdgeInsets.symmetric(horizontal: 16.0, vertical: 6.0);
