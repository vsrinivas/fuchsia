// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxtest/fxtest.dart';

/// Drills into a [Map] through a given sequence of keys.
/// These keys should be [String] values for [Map] keys, or [int] values
/// for array indices.
///
/// If provided keys do not exist in a map, [fallback] will be returned.
///
/// If bad indices are provided when encountering a map, a [BadMapPathException]
/// will be thrown.
///
/// And lastly, if *extra* keys are found (a literal is encountered before all
/// keys are used), a [BadMapPathException] will be thrown.
///
/// Usage:
/// ```dart
/// var data = <String, dynamic>{
///   'key': {
///     'nested': 'value',
///   },
///   'list': ['of', 'data'],
///   'some_numbers': [1, 3, 5],
/// };
///
/// >>> getMapPath<String>(data, ['key', 'nested'])
/// 'value'
///
/// >>> getMapPath<String>(data, ['list', 1])
/// 'data'
///
/// >>> getMapPath<String>(data, ['missing', 'path'])
/// null
///
/// >>> getMapPath<String>(data, ['missing', 'path'], fallback: 'A safe value')
/// 'A safe value'
///
/// >>> getMapPath<int>(data, ['some_numbers', 1])
/// 3
///
/// >>> getMapPath<String>(data, ['list', 7])
/// // throws BadMapPathException()
/// ```
T getMapPath<T>(
  Map<String, dynamic> map,
  List<dynamic> keys, {
  // Optional default value in case the requisite entries are missing
  T fallback,
  // If supplied, casts non-null values into their correct type
  T Function(dynamic val) caster,
}) {
  if (map == null) {
    return fallback;
  }

  final dynamic key = keys[0];
  final dynamic value = map[key];

  if (key is! String || value == null || (value is String && value.isEmpty)) {
    return fallback;
  }
  // We made it to the end!
  if (keys.length == 1) {
    if (caster == null) return value;
    if (value is T) return value;
    return caster(value);
  } else if (value is Map) {
    return getMapPath(
      value,
      keys.sublist(1, keys.length),
      fallback: fallback,
      caster: caster,
    );
  } else if (value is List) {
    return _getListPath(
      value,
      keys.sublist(1, keys.length),
      fallback: fallback,
      caster: caster,
    );
  }
  if (fallback != null) return fallback;
  throw BadMapPathException(
    'Reached literal value before exhausting all keys',
  );
}

/// [List]-based helper for [getMapPath].
///
/// Note: This function used to be woven into [getMapPath]'s own logic, but
/// along with leading to unwieldy code, this also created complications when
/// needing to handle lists-within-lists.
T _getListPath<T>(
  List<dynamic> data,
  List<dynamic> keys, {
  // Optional default value in case the requisite entries are missing
  T fallback,
  // If supplied, casts non-null values into their correct type
  T Function(dynamic val) caster,
}) {
  final key = keys.first;

  // Length and type checks first, since unlike Maps which return `null` for bad
  // keys, Lists throw exceptions.
  if (key is! int) {
    if (fallback != null) return fallback;
    throw BadMapPathException('Reached list value without integer key');
  }
  if (key >= data.length) {
    return fallback;
  }

  final value = data[key];

  // We made it to the end!
  if (keys.length == 1) {
    if (caster == null) return value;
    if (value is T) return value;
    return caster(value);
  } else if (value is Map) {
    return getMapPath<T>(
      value,
      keys.sublist(1),
      fallback: fallback,
      caster: caster,
    );
  } else if (value is List) {
    return _getListPath<T>(
      value,
      keys.sublist(1),
      fallback: fallback,
      caster: caster,
    );
  }
  if (fallback != null) return fallback;
  throw BadMapPathException(
    'Reached literal value before exhausting all keys',
  );
}
