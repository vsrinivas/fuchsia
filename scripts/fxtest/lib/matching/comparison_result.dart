// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

class ComparisonResult {
  /// Certainty in a positive match. Values between 0 and 1, where 0 implies
  /// [isMatch] must equal [false], and 1 implies a direct string (or substring)
  /// match.
  ///
  /// This score is:
  ///   a) only relevant during fuzzy matching (it is always 1 or 0 during
  ///      strict matching), and
  ///   b) relative based on our threshold for a given match.
  ///
  /// For example, confidence of 0.1 means something like our Levenshtein
  /// distance threshold for success was <=10 and this comparison had a distance
  /// of 9. It could also mean our threshold was <=50 and this comparison had
  /// distance of 45, or any other similar ratio.
  final double confidence;

  static final ComparisonResult failure = ComparisonResult._failure();

  const ComparisonResult._failure() : confidence = 0;
  const ComparisonResult.strict({bool isMatch}) : confidence = isMatch ? 1 : 0;
  const ComparisonResult.withConfidence(this.confidence);

  factory ComparisonResult.fromAverage(List<ComparisonResult> results) {
    double totalConfidence = 0;
    // Sum the total confidence
    for (var result in results) totalConfidence += result.confidence;
    // Complete average calculation
    return ComparisonResult.withConfidence(totalConfidence / results.length);
  }

  /// All gte-zero confidences indicate a positive comparison.
  bool get isMatch => confidence > 0;

  static ComparisonResult bestResult(
    ComparisonResult result1,
    ComparisonResult result2,
  ) =>
      result1.confidence > result2.confidence ? result1 : result2;

  @override
  String toString() => '<ComparisonResult: $confidence />';
}
