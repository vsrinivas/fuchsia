// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

/// Holds the current [Locale] and notifies any listeners of value changes.
class CurrentLocale extends ValueNotifier<Locale> {
  /// Constructs a new `CurrentLocale`.
  CurrentLocale(Locale value) : super(value);

  /// Returns the Unicode locale of the current locale as a string.  E.g.
  /// "en_US".
  String unicode() =>
      // Locale.toString() is for debugging purposes only, but it's the
      // correct form.
      Intl.canonicalizedLocale(value.toString());
}
