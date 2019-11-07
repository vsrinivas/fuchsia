// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an L10N developer experience study fragment.
// Let's see how it works when you offload all strings to a file.

// The Intl.message texts have been offloaded to a separate file so as to
// minimize the amount of code needed to be exported for localization.
// While the individual static String get messages have been left in their original form,
// the case-ness of the text is part of the presentation so should probably be
// handled in the view code.

import 'package:intl/intl.dart';

/// Provides access to localized strings used in Ermine code.
class Strings {
  static final Strings instance = Strings._internal();
  factory Strings() => instance;
  Strings._internal();

  static String get back => Intl.message(
        'Bck',
        name: 'back',
        desc: 'A very short label meaning "Go back (to previous web page)"',
      );
  static String get forward => Intl.message(
        'Fwd',
        name: 'forward',
        desc: 'A very short label meaning "Go forward (to the next web page)"',
      );
  static String get refresh => Intl.message(
        'Rfrsh',
        name: 'refresh',
        desc: 'A very short label meaning "Refresh the web page"',
      );
  static String get search => Intl.message(
        'Search',
        name: 'search',
        desc:
            'A regular length string label appearing in the browser search bar',
      );
  static String get browser => Intl.message(
        'Browser',
        name: 'browser',
        desc: 'As in: web browser',
      );
}
