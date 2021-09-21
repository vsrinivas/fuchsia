// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/material.dart' hide Intent;

import 'slider_scaffold.dart';

class App extends StatelessWidget {
  const App({
    Key key,
  }) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SliderScaffold();
  }
}
