// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import './comparison_result.dart';

/// Comparison helper which allows other tools to easily assess whether items
/// are "alike" on variable criteria.
abstract class Comparer {
  ComparisonResult equals(String haystack, String needle);
  ComparisonResult contains(String haystack, String needle);
  ComparisonResult startsWith(String haystack, String needle);
  ComparisonResult endsWith(String haystack, String needle);
}

/// Comparer with zero creativity. Either the strings match or they don't.
class StrictComparer extends Comparer {
  @override
  ComparisonResult equals(String haystack, String needle) =>
      ComparisonResult.strict(isMatch: haystack == needle);
  @override
  ComparisonResult contains(String haystack, String needle) =>
      ComparisonResult.strict(isMatch: haystack.contains(needle));
  @override
  ComparisonResult startsWith(String haystack, String needle) =>
      ComparisonResult.strict(isMatch: haystack.startsWith(needle));
  @override
  ComparisonResult endsWith(String haystack, String needle) =>
      ComparisonResult.strict(isMatch: haystack.endsWith(needle));
}
