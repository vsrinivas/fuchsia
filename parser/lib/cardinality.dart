// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This file defines the Cardinality type used in the parse tree of
/// modular semantic label expressions and some functions that implement
/// its semantics.

/// The cardinality used in modular semantic label expressions.
class Cardinality {
  final bool _optional;
  final bool _repeated;

  Cardinality({optional: false, repeated: false})
      : _optional = optional ?? false,
        _repeated = repeated ?? false;

  Cardinality sum(final Cardinality other) => new Cardinality(
      optional: _optional || other._optional,
      repeated: _repeated || other._repeated);

  factory Cardinality.fromJson(Map<String, bool> values) {
    return new Cardinality(
        optional: values['optional'], repeated: values['repeated']);
  }

  dynamic toJson() => {'optional': _optional, 'repeated': _repeated};

  static final singular = new Cardinality();
  static final optional = new Cardinality(optional: true);
  static final repeated = new Cardinality(repeated: true);
  static final optionalRepeated =
      new Cardinality(optional: true, repeated: true);

  @override
  String toString() =>
      _optional ? (_repeated ? '*' : '?') : (_repeated ? '+' : '');

  @override
  bool operator ==(other) =>
      other is Cardinality &&
      _optional == other._optional &&
      _repeated == other._repeated;

  @override
  int get hashCode {
    return (_optional ? 1 : 0) + (_repeated ? 2 : 0);
  }

  bool get isRepeated => _repeated;

  bool get isOptional => _optional;
}
