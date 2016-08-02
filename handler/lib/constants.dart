// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Contains frequently used constants that are internal to Modular.
class Constants {
  static const String entityTimestampLabel = 'internal:last_modified_timestamp';

  static const String metadataLabel = 'internal:metadata';
  static const String recipeLabel = 'internal:recipe';

  static const String sessionGraphLabelPrefix = 'internal:session_graph:';
  static const String sessionGraphIdLabel = '${sessionGraphLabelPrefix}id';

  static const String suggestionDisplayLabel =
      'https://github.com/domokit/modular/wiki/display#suggestion';
  static const String suggestionIdLabel = 'internal:live_suggestion_id';

  // Built-in representation types
  static const String dateTimeRepresentationLabel =
      'https://github.com/domokit/modular/wiki/representation#date-time';
  static const String intRepresentationLabel =
      'https://github.com/domokit/modular/wiki/representation#int';
  static const String stringRepresentationLabel =
      'https://github.com/domokit/modular/wiki/representation#string';
}
