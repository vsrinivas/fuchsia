// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'button.dart';
import 'layout.dart';

/// Button with a text label only without background color or borders.
class TextOnlyButton extends ErmineButton {
  final Size _minSize;

  const TextOnlyButton._(
      String label, VoidCallback onTap, TextStyle textStyle, this._minSize,
      {Key? key})
      : super(label, onTap, textStyle, key: key);

  factory TextOnlyButton.small(String label, VoidCallback onTap, {Key? key}) =>
      TextOnlyButton._(label, onTap, kLabelSmallTextStyle, kMinSizeSmall,
          key: key);

  factory TextOnlyButton.medium(String label, VoidCallback onTap, {Key? key}) =>
      TextOnlyButton._(label, onTap, kLabelMediumTextStyle, kMinSizeMedium,
          key: key);

  factory TextOnlyButton.large(String label, VoidCallback onTap, {Key? key}) =>
      TextOnlyButton._(label, onTap, kLabelLargeTextStyle, kMinSizeLarge,
          key: key);

  @override
  Widget build(BuildContext context) => TextButton(
        onPressed: onTap,
        child: Padding(
          padding: kButtonMargins,
          child: Text(label.toUpperCase()),
        ),
        style: ButtonStyle(
          alignment: Alignment.center,
          foregroundColor: MaterialStateProperty.resolveWith<Color>(
            (Set<MaterialState> states) {
              if (states.contains(MaterialState.disabled))
                return kDisabledColor;
              else if (states.contains(MaterialState.hovered))
                return kHoverColor;
              return kNormalColor;
            },
          ),
          overlayColor: MaterialStateProperty.all<Color>(Colors.transparent),
          textStyle: MaterialStateProperty.all<TextStyle>(textStyle),
          minimumSize: MaterialStateProperty.all<Size>(_minSize),
          padding: MaterialStateProperty.all<EdgeInsets>(EdgeInsets.all(0)),
        ),
      );
}
