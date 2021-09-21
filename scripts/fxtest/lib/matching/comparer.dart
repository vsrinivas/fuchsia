// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:math';

import 'package:edit_distance/edit_distance.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

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

/// Comparer backed by Levenshtein distance to suggest possible typos after
/// the test suite matches zero tests.
///
/// To support the desire to match substrings (particularly of long paths),
/// this will look for slashes in the [needle] and consume as many slashes in
/// the [haystack]. For example the following example will calculate a
/// Levenshtein distance of 1 and then, with that below the threshold of 3,
/// return `true`.
///
/// ```dart
/// FuzzyComparer({threshold: 3}).endsWith(
///   '/some/long/path/to/our/binary',
///   'out/binary',
/// )
/// >>> true
/// ```
///
/// This is because the [FuzzyComparer] will detect that there is one slash in
/// the [needle] thus, internally, run the following evaluation:
///
/// ```dart
/// levenshtein('our/binary', 'out/binary')
/// ```
///
/// Behavior is more involved with the [contains] method because there we cannot
/// assume the starting or ending substrings. Thus, [contains] counts the slashes
/// in the [needle] and compares against all possible chunks of [haystack].
/// Thus, our previous example, swapped for [contains] instead of [endsWith],
/// would run all of the following comparisons:
///
/// ```dart
/// return min([
///   levenshtein('some/long', 'out/binary'),
///   levenshtein('long/path', 'out/binary'),
///   levenshtein('path/to', 'out/binary'),
///   levenshtein('to/our', 'out/binary'),
///   levenshtein('our/binary', 'out/binary'),
/// ]);
/// ```
///
/// This behavior should hopefully "Get to Yes" in terms of helpful
/// "Did you mean?" suggestions without too many false-positives.
class FuzzyComparer extends Comparer {
  /// Levenshtein distance cutoff for considering two values a match.
  /// EXCLUSIVE.
  final int threshold;

  static final Levenshtein _lev = Levenshtein();

  /// Value returned when either or both strings are null.
  static const int maxDistance = 999;

  static const String separator = '/';

  /// Whether the levenshtein algorithm should consider characters different
  /// if they are the same letter, but upper and lower case.
  final bool caseSensitive;
  FuzzyComparer({
    @required this.threshold,
    this.caseSensitive = true,
  });

  int _levDistance(String h, String n) => (h == null || n == null)
      ? FuzzyComparer.maxDistance
      : _lev.distance(
          caseSensitive ? h : h.toLowerCase(),
          caseSensitive ? n : n.toLowerCase(),
        );

  /// Due to the chunking nature of our matching, leading or trailing slashes
  /// in the needle will cause false-negatives.
  String _prepareNeedle(String needle) {
    var _needle = needle;
    if (_needle == null) return _needle;
    if (_needle.startsWith(separator)) {
      _needle = _needle.substring(1);
    }
    if (_needle.endsWith(separator)) {
      _needle = _needle.substring(0, _needle.length - 1);
    }
    return _needle;
  }

  double computeConfidence(int levenshteinDistance) =>
      levenshteinDistance >= threshold
          ? 0
          : (threshold - levenshteinDistance) / threshold;

  @override
  ComparisonResult contains(String haystack, String needle) {
    final _needle = _prepareNeedle(needle);
    var chunks = chunkOnSlashes(haystack, _needle);
    int minDistance = max(haystack.length, _needle.length);
    for (var chunk in chunks) {
      minDistance = min(minDistance, _levDistance(chunk, _needle));
    }
    return ComparisonResult.withConfidence(computeConfidence(minDistance));
  }

  @override
  ComparisonResult endsWith(String haystack, String needle) {
    final _needle = _prepareNeedle(needle);
    var endingChunk = chunkOnSlashes(haystack, _needle).last;
    return ComparisonResult.withConfidence(
      computeConfidence(_levDistance(endingChunk, _needle)),
    );
  }

  @override
  ComparisonResult equals(String haystack, String needle) {
    final _needle = _prepareNeedle(needle);
    return ComparisonResult.withConfidence(
      computeConfidence(_levDistance(haystack, _needle)),
    );
  }

  @override
  ComparisonResult startsWith(String haystack, String needle) {
    final _needle = _prepareNeedle(needle);
    var startingChunk = chunkOnSlashes(haystack, _needle, limit: 1).first;
    return ComparisonResult.withConfidence(
      computeConfidence(_levDistance(startingChunk, _needle)),
    );
  }
}

/// Divides the [haystack] into the maximum number of substrings that contain a given
/// number of a separator. That number of separators is determined by the number
/// of its occurances in the [needle].
///
/// Note that [needle] does not need to be a substring of [haystack] -- all that
/// matters is its count of [sep].
///
/// Usage:
/// ```dart
/// // "a/file" contains one slash
/// chunkOnSlashes('some/path/to/a/file', 'a/file', sep: '/')
/// // So we return all possible substrings containing one slash
/// >>> ['some/path', 'path/to', 'to/a', 'a/file']
/// ```
List<String> chunkOnSlashes(
  /// String we intend to split into a list of strings.
  String haystack,

  /// String we will scan for instances of [sep] to determine how to split
  /// [haystack].
  String needle, {

  /// Separator string that defaults to the current OS's filesystem separator.
  String sep,

  /// Optional limit on the number of results to return. For example, if you
  /// know you only care about the first result, pass 1 to skip calculating and
  /// throwing away subsequent results.
  int limit,
}) {
  sep ??= p.separator;

  var chunks = <String>[];
  if (haystack == null) {
    return chunks;
  }
  if (needle == null) return [haystack];

  int numSlashes = needle.split(sep).length - 1;
  var splitHaystack = haystack.split(sep);

  if (numSlashes >= splitHaystack.length) {
    return [haystack];
  }

  var counter = 0;
  while (counter <= splitHaystack.length - numSlashes - 1) {
    var endIndex = counter + numSlashes + 1;
    chunks.add(splitHaystack.sublist(counter, endIndex).join(sep));
    counter += 1;

    if (limit != null && chunks.length >= limit) {
      break;
    }
  }

  // Briefly cast to a set to remove duplicates
  return chunks.toSet().toList();
}
