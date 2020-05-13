// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

/// Builds [builder] if [condition] is true.
class ConditionalBuilder extends StatelessWidget {
  /// If true, [build] uses [builder] to build.
  final bool condition;

  /// Builds the [Widget] if [condition] is true.
  final WidgetBuilder builder;

  /// Place holder [Widget] if [condition] is false.
  final Widget placeHolder;

  /// Constructor.
  const ConditionalBuilder(
      {this.condition, this.builder, this.placeHolder = const Offstage()});

  @override
  Widget build(BuildContext context) =>
      condition ? builder(context) : placeHolder;
}
