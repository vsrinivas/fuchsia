// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:meta/meta.dart';

class TestArguments {
  final List<String> rawArgs;

  /// Arguments hydrated into their respective data type representations
  /// ([bool], [String], [int], etc).
  final ArgResults parsedArgs;

  /// Raw string arguments to be forwarded down to each executed test.
  ///
  /// These tokens have no impact whatsoever on how `fx test` finds, filters,
  /// and invokes tests. They are exclusively used by the underlying tests.
  final List<String> passThroughArgs;

  /// Optional strings to pretend were passed. Key-value pairs are merged into
  /// a single string in [rawArgs] if the key is not present in [rawArgs].
  /// For flags (as opposed to options), pass either `null` or an empty string
  /// as the value.
  final Map<String, String> defaultRawArgs;

  /// Similar to [passThroughArgs], but unique to e2e tests.
  final List<String> e2ePassThroughArgs;

  TestArguments({
    @required this.rawArgs,
    @required ArgParser parser,
    this.defaultRawArgs,
  })  : e2ePassThroughArgs = <String>[],
        passThroughArgs = TestArguments._extractPassThroughArgs(rawArgs),
        parsedArgs = TestArguments._parseArgs(
          rawArgs,
          parser,
          defaultRawArgs: defaultRawArgs,
        );

  /// Splits a list of command line arguments into the half intended for
  /// local use and the half intended to be passed through to sub-commands.
  static List<List<String>> splitArgs(List<String> rawArgs) {
    var dashDashIndex = rawArgs.indexOf('--');
    if (dashDashIndex == -1) {
      dashDashIndex = rawArgs.length;
    }
    return [
      rawArgs.take(dashDashIndex).toList(),
      rawArgs.skip(dashDashIndex + 1).toList(),
    ];
  }

  static List<String> _extractPassThroughArgs(List<String> rawArgs) {
    return TestArguments.splitArgs(rawArgs)[1];
  }

  static ArgResults _parseArgs(
    List<String> rawArgs,
    ArgParser parser, {
    Map<String, String> defaultRawArgs,
  }) {
    var localArgs = TestArguments.splitArgs(rawArgs)[0];
    localArgs = TestArguments.addDefaults(localArgs, defaultRawArgs, parser);
    return parser.parse(localArgs);
  }

  // ignore: prefer_constructors_over_static_methods
  static List<String> addDefaults(
    List<String> rawArgs,
    Map<String, String> defaults,
    ArgParser parser,
  ) {
    if (defaults == null || defaults.isEmpty) return rawArgs;

    var copy = List<String>.from(rawArgs);
    const String invertToken = '--no-';
    for (var key in defaults.keys) {
      // For a default of "--flag", we only want to add if there is no "--flag"
      // and no "--no-flag". The inverse is also true, for a default of
      // "--no-flag", we have to check for both "--no-flag" and "--flag".
      final oppositeKey = key.startsWith(invertToken)
          // Opposite of an invert is to drop the invert and re-apply "--".
          ? '--${key.substring(invertToken.length)}'
          // Opposite of a positive is to drop the leading "--" then add the
          // inversion token.
          : '$invertToken${key.substring(2)}';
      final keysToCheckFor = <String>{key, oppositeKey};

      // Grab the shorter key, which will be the one without a leading `--no-`.
      // We need this to check for an abbreviation.
      var primaryKey = key.length < oppositeKey.length ? key : oppositeKey;
      // Trim leading "--"
      primaryKey = primaryKey.substring(2);
      if (parser.options[primaryKey]?.abbr != null) {
        keysToCheckFor.add('-${parser.options[primaryKey].abbr}');
      }
      if (Set.from(rawArgs).intersection(keysToCheckFor).isEmpty) {
        copy.add(key);
        if (defaults[key] != null && defaults[key] != '') {
          copy.add(defaults[key]);
        }
      }
    }
    return copy;
  }
}
