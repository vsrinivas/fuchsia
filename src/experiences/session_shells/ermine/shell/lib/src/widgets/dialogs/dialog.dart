// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// An abstract class for all Dialog widgets that will be carried by
/// [AppState.dialogs].
abstract class Dialog extends StatelessWidget {
  const Dialog({Key? key}) : super(key: key);
}
