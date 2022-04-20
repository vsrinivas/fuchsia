// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A wrapper type for optional values that may also be `null`. This generic
/// class can be used in argument lists for optional arguments. When called, the
/// argument state will be one of:
///
///   * `undefined` - The caller did not provide a new value
///   * Some(value) - The caller provided a new, non-`null` value
///   * None() - The caller provided a `null` value
///
/// A common usage is:
///
/// ```dart
/// class MyClass {
///   String? maybeStr;
///   List<int>? maybeList;
///   Map<String, String>? maybeMap;
///
///   MyClass({this.maybeStr, this.maybeList, this.maybeMap});
///
///   MyClass cloneWithOverrides({
///     OptionalNullable<String> maybeStr = const OptionalNullable.undefined(),
///     OptionalNullable<List<int>> maybeList = const OptionalNullable.undefined(),
///     OptionalNullable<Map<String, String>> maybeMap = const OptionalNullable.undefined(),
///   }) {
///     return MyClass(
///       maybeStr: maybeStr.or(this.maybeStr),
///       maybeList: maybeList.or(this.maybeList),
///       maybeMap: maybeMap.or(this.maybeMap),
///     );
///   }
/// }
///
/// main() {
///   final orig = MyClass(
///     maybeStr: null,
///     maybeList: [1, 2, 3],
///     maybeMap: {'door': 'wood', 'window': 'glass'},
///   );
///   final mod = orig.cloneWithOverrides(
///     maybeStr: Some('a string'),
///     maybeMap: None(),
///   );
///   assert(mod.maybeStr == 'a string');
///   assert(mod.maybeList!.length == 3);
///   assert(mod.maybeMap == null);
/// }
/// ```
class OptionalNullable<T> {
  final T? _value;

  const OptionalNullable._(this._value);

  /// Initialize an [OptionalNullable] to an undefined state. The value of
  /// an [OptionalNullable] is considered "undefined" until overridden by
  /// assigning it to [Some] or [None].
  const OptionalNullable.undefined() : _value = null;

  /// True if the value is [Some] or [None].
  bool get isDefined => this is Some || this is None;

  /// True if the value is not [Some] or [None].
  bool get isUndefined => !isDefined;

  /// If the value is [Some], the value is returned. If [None], `null` is
  /// returned. Otherwise, the value `isUndefined`, in which case the given
  /// [fallback] value is returned instead.
  T? or(T? fallback) {
    if (isDefined) {
      return _value;
    } else {
      return fallback;
    }
  }
}

/// A value wrapped in [Some] can be assigned to an [OptionalNullable] to
/// convey the value should be used instead of a default.
class Some<T> extends OptionalNullable<T> {
  const Some(T value) : super._(value);
}

/// [None] can be assigned to an [OptionalNullable] to convey that `null`
/// should be used instead of a default.
class None<T> extends OptionalNullable<T> {
  const None() : super._(null);
}
