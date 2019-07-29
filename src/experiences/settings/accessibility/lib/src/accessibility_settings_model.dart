// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';
import 'package:lib.widgets/model.dart';

class AccessibilitySettingsModel extends Model {
  final ValueNotifier<bool> screenReaderEnabled = ValueNotifier<bool>(false);

  final ValueNotifier<bool> colorInversionEnabled = ValueNotifier<bool>(false);

  final ValueNotifier<bool> magnificationEnabled = ValueNotifier<bool>(false);

  final ValueNotifier<int> magnificationZoom = ValueNotifier<int>(100);

  AccessibilitySettingsModel() {
    screenReaderEnabled.addListener(notifyListeners);
    colorInversionEnabled.addListener(notifyListeners);
    magnificationEnabled.addListener(notifyListeners);
    magnificationZoom.addListener(notifyListeners);
  }
}
