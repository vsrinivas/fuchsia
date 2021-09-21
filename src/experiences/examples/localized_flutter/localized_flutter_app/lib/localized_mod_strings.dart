// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:intl/intl.dart';

// ignore: avoid_classes_with_only_static_members

/// A container for strings used by Localized Mod.
/// If the app were larger, there would be a separate __Strings class for each
/// package that needed one.
class LocalizedModStrings {
  static String get appTitle => Intl.message(
        'Localized Mod',
        name: 'appTitle',
        desc: 'Title of the demo application. '
            '"Mod" is short for "module" (can also be translated as "app").',
      );

  static String bodyText(int itemCount) => Intl.plural(
        itemCount,
        zero: 'There are no messages.',
        one: 'There is one message.',
        other: 'There are $itemCount messages.',
        name: 'bodyText',
        args: [itemCount],
        desc: 'How many messages are in the list.',
        examples: const {'itemCount': 42},
      );

  static String get footer => Intl.message(
        'Coming soon: actually reading those messages!',
        name: 'footer',
        desc: 'Footer text of the demo application. '
            'Implies that messages cannot currently be read.',
      );
}
