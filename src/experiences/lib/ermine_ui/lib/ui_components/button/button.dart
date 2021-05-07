// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// An abstract class that defines the common UI fields of Ermine's text buttons.
///
/// Parent of [BorderedButton], [FilledButton], and [TextOnlyButton].
abstract class ErmineButton extends StatelessWidget {
  final String label;
  final VoidCallback onTap;
  final TextStyle textStyle;

  const ErmineButton(this.label, this.onTap, this.textStyle, {Key? key})
      : super(key: key);
}
