// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/widgets.dart';

// Ideally, this file should be generated at build time from a build variable.
const List<Locale> supportedLocales = <Locale>[
  Locale('en'),
  Locale('ar', 'XB'),
  Locale('en', 'XA'),
  Locale('en', 'XC'),
  // Note: There is no messages_es.dart checked in. Creating this file is
  // left as an exercise for those working on integrating Fuchsia with a
  // translation pipeline.
  Locale('es'),
  Locale('he'),
  Locale('sr'),
  // Here is how to to specify a locale with a script code.
  Locale.fromSubtags(languageCode: 'sr', scriptCode: 'Latn', countryCode: 'RS')
];
