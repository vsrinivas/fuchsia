// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

class ListIterator<T> {
  final List<T> _list;
  int _counter = 0;
  ListIterator._(this._list);
  factory ListIterator.from(List<T> list) => ListIterator<T>._(list);

  /// Look at the next item in the list without advancing the marker.
  T? peek() => _list.length > _counter ? _list[_counter] : null;

  /// Look at the next item in the list and advance the marker.
  T? takeNext() {
    if (_counter == _list.length) return null;
    _counter += 1;
    return _list[_counter - 1];
  }

  List<T> take(int number) {
    final _oldCounter = _counter;
    _counter += number;
    return _list.sublist(_oldCounter, min(_counter, _list.length));
  }

  bool get isEmpty => hasMore();
  bool get isNotEmpty => !isEmpty;

  bool hasMore() => hasNMore(1);
  bool hasNMore(int numberMore) => _list.length - _counter >= numberMore;
}
