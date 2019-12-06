// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Container for all information needed to invoke and run a test, paired with
/// any warning information the user needs to see.
class CommandTokens {
  /// Complete list of items needed to invoke and run a test.
  final List<String> tokens;

  /// Optional warning to display to the user.
  ///
  /// This is a great place to put deprecation warnings or non-fatal malformed
  /// test issues.
  final String warning;

  CommandTokens(this.tokens, {this.warning});
  CommandTokens.empty()
      : tokens = [],
        warning = null;

  String get command => tokens.first;
  List<String> get args => tokens.sublist(1);
  String get fullCommand => tokens.join(' ').trim();
}
