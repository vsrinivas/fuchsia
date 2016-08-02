// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/constants.dart';
import 'package:parser/expression.dart';

/// Helper function to find and obtain a path expression containing the
/// 'suggestion' display label.
PathExpr findSuggestionDisplayLabel(final Iterable<PathExpr> exprs) {
  return exprs.firstWhere(
      (final PathExpr e) =>
          e.containsLabelAsString(Constants.suggestionDisplayLabel),
      orElse: () => null);
}

/// Returns true if the given list of path expressions contains the 'suggestion'
/// display label
bool hasSuggestionDisplayLabel(final Iterable<PathExpr> exprs) {
  return findSuggestionDisplayLabel(exprs) != null;
}
