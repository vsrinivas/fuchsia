// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:yaml/yaml.dart';

/// Indicates a location in a source file. Instances are immutable because all
/// fields are final and of scalar types.
class SourceLocation {
  /// URL of the yaml file parsed.
  final String file;

  /// Line in the yaml file, starting at 1.
  final int line;

  /// Column in that line, starting at 1.
  final int column;

  /// Captures the source location of a yaml node. We add +2 to the line counted
  /// by the yaml parser, because the yaml parser counts lines starting at 0,
  /// and we always strip off the #! line before passing the yaml file to the
  /// yaml parser. TODO(mesch): This micounts in the case where the yaml text
  /// doesn't come from a yaml file (such as in tests), or when there is no #!
  /// line to strip off.
  SourceLocation.yaml(final YamlNode yaml)
      : file = yaml.span.sourceUrl?.toString(),
        line = yaml.span.start.line + 2,
        column = yaml.span.start.column + 1;

  /// Captures the source location inside an expression in the value of a yaml
  /// node.
  SourceLocation.expr(final SourceLocation base, final int pos)
      : file = base.file,
        line = base.line,
        column = base.column + pos;

  /// Source location used for an expression parsed from a string literal inline
  /// in dart code.
  const SourceLocation.inline()
      : file = 'inline',
        line = 0,
        column = 0;

  @override
  String toString() => '($file:$line,$column)';
}

class ParseError extends Error {
  final SourceLocation location;
  final String report;

  ParseError.atLocation(this.location, this.report);

  ParseError.atNode(final YamlNode node, this.report)
      : location = new SourceLocation.yaml(node);

  @override
  String toString() => '$location $report';
}
