// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'layout.dart';

/// Radio in the Terminal Chic style for Ermine
///
/// Each fields does the same thing that the equivalent field in the Flutter's
/// [Radio] does. Some fields like the ones for changing colors are immutable,
/// and therefore, not provided by [ErmineRadio].
class ErmineRadio<T> extends StatelessWidget {
  final T value;
  final T groupValue;
  final ValueChanged<T?>? onChanged;
  final bool autofocus;
  final FocusNode? focusNode;
  final MouseCursor? mouseCursor;

  const ErmineRadio({
    required this.value,
    required this.groupValue,
    this.onChanged,
    this.autofocus = false,
    this.focusNode,
    this.mouseCursor,
    Key? key,
  }) : super(key: key);

  @override
  Widget build(BuildContext context) => Theme(
        data: Theme.of(context).copyWith(
          unselectedWidgetColor: kToggleActive,
          disabledColor: kToggleDisabled,
        ),
        child: Radio<T>(
          value: value,
          groupValue: groupValue,
          onChanged: onChanged,
          activeColor: kToggleActive,
          splashRadius: kSplashRadius,
          autofocus: autofocus,
          focusNode: focusNode,
          mouseCursor: mouseCursor,
        ),
      );
}
