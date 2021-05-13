// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../widgets/oobe/channel.dart';
// TODO(fxbug.dev/73407): Complete data sharing feature.
//import '../widgets/oobe/data_sharing.dart';
import '../widgets/oobe/ssh_keys.dart';

/// Model that manages the state of the OOBE UX.
class OobeModel {
  /// Provides a way to exit [Oobe].
  final VoidCallback onFinished;

  List<Widget> oobeItems;

  ValueNotifier<int> currentItem = ValueNotifier(0);

  /// Constructor.
  OobeModel({@required this.onFinished});

  void loadOobeItems() {
    oobeItems = [
      Channel.withSvcPath(onBack, onNext),
      // TODO(fxbug.dev/73407): Complete data sharing feature.
      // DataSharing.withSvcPath(onBack, onNext),
      SshKeys.withSvcPath(onBack, onNext),
    ];
  }

  /// Go to the next screen. If we have reached the final screen, exit [Oobe].
  void onNext() {
    // Check if we want to exit first so we don't trigger a rebuild and index
    // error by changing currentItem.
    if (currentItem.value + 1 == oobeItems.length) {
      onFinished();
      return;
    }
    currentItem.value++;
  }

  /// Go to the previous screen. If we are on the first screen do nothing.
  void onBack() {
    if (currentItem.value == 0) {
      return;
    }
    currentItem.value--;
  }
}
