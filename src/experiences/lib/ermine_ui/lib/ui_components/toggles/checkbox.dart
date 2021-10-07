// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'layout.dart';

/// Checkbox in the Terminal Chic style for Ermine
///
/// Each fields does the same thing that the equivalent field in the Flutter's
/// [Checkbox] does. Some fields like the ones for changing colors are immutable,
/// and therefore, not provided by [ErmineCheckbox].
class ErmineCheckbox extends StatelessWidget {
  final bool value;
  final ValueChanged<bool?>? onChanged;
  final bool tristate;
  final bool autofocus;
  final FocusNode? focusNode;
  final MouseCursor? mouseCursor;

  const ErmineCheckbox({
    required this.value,
    this.onChanged,
    this.tristate = false,
    this.autofocus = false,
    this.focusNode,
    this.mouseCursor,
    Key? key,
  }) : super(key: key);

  @override
  Widget build(BuildContext context) => Stack(
        alignment: Alignment.center,
        children: [
          Container(
            width: kCheckboxSize,
            height: kCheckboxSize,
            decoration: BoxDecoration(
              border: Border.all(
                color: onChanged != null ? kToggleActive : kToggleDisabled,
                width: kCheckboxBorderWidth,
              ),
            ),
          ),
          Theme(
            data: Theme.of(context).copyWith(
              unselectedWidgetColor: kCheckboxHide,
              disabledColor: kCheckboxHide,
            ),
            child: Checkbox(
              value: value,
              onChanged: onChanged,
              activeColor: kCheckboxHide,
              checkColor: onChanged != null ? kToggleActive : kToggleDisabled,
              splashRadius: kSplashRadius,
              tristate: tristate,
              autofocus: autofocus,
              focusNode: focusNode,
              mouseCursor: mouseCursor,
            ),
          ),
        ],
      );
}
