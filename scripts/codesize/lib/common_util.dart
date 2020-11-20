// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

String removeSuffix(String s, String suffix) {
  if (s.endsWith(suffix)) {
    return s.substring(0, s.length - suffix.length);
  }
  throw Exception('$s did not end with $suffix');
}

String maybeRemoveSuffix(String s, String suffix) {
  if (s.endsWith(suffix)) {
    return s.substring(0, s.length - suffix.length);
  }
  return s;
}

String removePrefix(String s, String prefix) {
  if (s.startsWith(prefix)) {
    return s.substring(prefix.length);
  }
  throw Exception('$s did not begin with $prefix');
}

String formatSize(num inSize) {
  num size = inSize;
  if (size < 1024) return '$size B';
  size /= 1024;
  if (size < 1024) return '${size.toStringAsFixed(1)} KiB';
  size /= 1024.0;
  if (size < 1024) return '${size.toStringAsFixed(1)} MiB';
  size /= 1024;
  return '${size.toStringAsFixed(1)} GiB';
}

R flatMap<T, R>(T input, R Function(T) fn) {
  if (input != null)
    return fn(input);
  else
    return null;
}

class Pair<T> {
  final T a;
  final T b;

  Pair(this.a, this.b);
}

Iterable<Pair<T>> zip<T>(Iterable<T> iterableA, Iterable<T> iterableB) sync* {
  final iterA = iterableA.iterator;
  final iterB = iterableB.iterator;
  while (iterA.moveNext() && iterB.moveNext()) {
    yield Pair<T>(iterA.current, iterB.current);
  }
}
