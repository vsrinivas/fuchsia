// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../common_util.dart';
import '../render/ast.dart';
import '../types.dart';
import 'index.dart';

class BinaryNamesQuery extends Query implements QueryReport {
  BinaryNamesQuery({this.sortBySize = false});

  final bool sortBySize;

  static const String description =
      'Shows the list of names of all binaries under analysis. '
      'Can be useful in determining what\'s in an image.';

  @override
  String getDescription() => description;

  @override
  void addReport(Report report) {
    _stats[report.context.name] = report.fileTotal;
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is BinaryNamesQuery) {
        _stats.addAll(other._stats);
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  @override
  QueryReport distill() => this;

  final _stats = <String, int>{};

  Function(MapEntry<String, int>, MapEntry<String, int>) get _compare =>
      sortBySize
          ? (a, b) {
              int sizeComp = -a.value.compareTo(b.value);
              if (sizeComp != 0) return sizeComp;
              return a.key.compareTo(b.key);
            }
          : (a, b) => a.key.compareTo(b.key);

  @override
  String toString() {
    return (_stats.entries.toList()
          ..sort(_compare)
          ..map((e) => ' - $e'))
        .join('\n');
  }

  @override
  Iterable<AnyNode> export() => [
        for (final entry in _stats.entries.toList()..sort(_compare))
          if (sortBySize)
            Node.plain('${entry.key} (${formatSize(entry.value)})')
          else
            Node.plain(entry.key)
      ];
}
