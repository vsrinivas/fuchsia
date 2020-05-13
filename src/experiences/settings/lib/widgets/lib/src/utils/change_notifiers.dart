import 'dart:math' as math;

import 'package:collection/collection.dart';
import 'package:flutter/foundation.dart';

/// List that notifies listeners on change.
class ChangeNotifierList<E> extends DelegatingList<E> with ChangeNotifier {
  /// Creates a new ChangeNotifierList, using baseList as the backing if
  /// included, otherwise constructing a new list.
  ChangeNotifierList([List<E> baseList]) : super(baseList ?? []);

  @override
  void operator []=(int index, E value) {
    if (super[index] != value) {
      super[index] = value;
      notifyListeners();
    }
  }

  @override
  void add(E value) {
    super.add(value);
    notifyListeners();
  }

  @override
  void addAll(Iterable<E> iterable) {
    super.addAll(iterable);
    notifyListeners();
  }

  @override
  void clear() {
    super.clear();
    notifyListeners();
  }

  @override
  void fillRange(int start, int end, [E fillValue]) {
    super.fillRange(start, end, fillValue);
    notifyListeners();
  }

  @override
  void insert(int index, E element) {
    super.insert(index, element);
    notifyListeners();
  }

  @override
  void insertAll(int index, Iterable<E> iterable) {
    super.insertAll(index, iterable);
    notifyListeners();
  }

  @override
  set length(int newLength) {
    if (length != newLength) {
      super.length = newLength;
      notifyListeners();
    }
  }

  @override
  bool remove(Object value) {
    final returnValue = super.remove(value);
    if (returnValue) {
      notifyListeners();
    }
    return returnValue;
  }

  @override
  E removeAt(int index) {
    final returnValue = super.removeAt(index);
    notifyListeners();
    return returnValue;
  }

  @override
  E removeLast() {
    final returnValue = super.removeLast();
    notifyListeners();
    return returnValue;
  }

  @override
  void removeRange(int start, int end) {
    super.removeRange(start, end);
    notifyListeners();
  }

  @override
  void removeWhere(bool test(E element)) {
    super.removeWhere(test);
    notifyListeners();
  }

  @override
  void replaceRange(int start, int end, Iterable<E> iterable) {
    super.replaceRange(start, end, iterable);
    notifyListeners();
  }

  @override
  void retainWhere(bool test(E element)) {
    super.retainWhere(test);
    notifyListeners();
  }

  @override
  void setAll(int index, Iterable<E> iterable) {
    super.setAll(index, iterable);
    notifyListeners();
  }

  @override
  void setRange(int start, int end, Iterable<E> iterable, [int skipCount = 0]) {
    super.setRange(start, end, iterable, skipCount);
    notifyListeners();
  }

  @override
  void shuffle([math.Random random]) {
    super.shuffle(random);
    notifyListeners();
  }

  @override
  void sort([int compare(E a, E b)]) {
    super.sort(compare);
    notifyListeners();
  }
}

/// Map that provides notification when changed.
class ChangeNotifierMap<K, V> extends DelegatingMap<K, V> with ChangeNotifier {
  /// Constructs a new [ChangeNotifierMap] using the base map as backing
  /// or a new map if none was provided.
  ChangeNotifierMap([Map<K, V> base]) : super(base ?? <K, V>{});

  @override
  void operator []=(K key, V value) {
    if (this[key] != value) {
      super[key] = value;
      notifyListeners();
    }
  }

  @override
  void addAll(Map<K, V> other) {
    super.addAll(other);
    notifyListeners();
  }

  // TODO: When addEntries is supported by [DelegatingMap], enable this.
  @override
  void addEntries(Iterable<Object> entries) {
    super.addEntries(entries);
    notifyListeners();
  }

  @override
  void clear() {
    super.clear();
    notifyListeners();
  }

  @override
  V putIfAbsent(K key, V ifAbsent()) {
    final isAbsent = containsKey(key);
    final returnValue = super.putIfAbsent(key, ifAbsent);
    if (isAbsent) {
      notifyListeners();
    }
    return returnValue;
  }

  @override
  V remove(Object key) {
    final returnValue = super.remove(key);
    notifyListeners();
    return returnValue;
  }

  @override
  void removeWhere(bool test(K key, V value)) {
    super.removeWhere(test);
    notifyListeners();
  }

  @override
  V update(K key, V update(V value), {V ifAbsent()}) {
    final returnValue = super.update(key, update, ifAbsent: ifAbsent);
    notifyListeners();
    return returnValue;
  }

  @override
  void updateAll(V update(K key, V value)) {
    super.updateAll(update);
    notifyListeners();
  }
}
