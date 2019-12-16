// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';

import 'package:io/ansi.dart';

// ignore: one_member_abstracts
abstract class Collector {
  Future<List<Item>> collect();
}

enum CategoryType {
  buildInfo,
  sourceInfo,
  environmentInfo,
  deviceInfo,
}

class Category {
  static const Map labels = {
    CategoryType.buildInfo: 'Build Info',
    CategoryType.sourceInfo: 'Source Info',
    CategoryType.environmentInfo: 'Environment Info',
    CategoryType.deviceInfo: 'Device Info',
  };

  final CategoryType type;
  final LinkedHashMap<String, Item> items = LinkedHashMap();

  Category(this.type);

  void add(Item item) {
    items[item.key] = item;
  }

  void addAll(List<Item> newItems) {
    // ignore: avoid_function_literals_in_foreach_calls
    newItems.forEach((i) {
      items[i.key] = i;
    });
  }

  Map<String, dynamic> toJson() => {
        'name': labels[type],
        'items': items,
      };

  @override
  String toString() {
    StringBuffer sb = StringBuffer()
      ..writeln(wrapWith('${labels[type]}:', [styleBold, styleUnderlined]))
      ..writeAll(items.values, '\n');
    return sb.toString();
  }
}

class Item<T> {
  Item(this.categoryType, this.key, this.title, this.value, [this.notes]);
  CategoryType categoryType;
  String title;
  String key;
  T value;
  String notes;

  Map<String, dynamic> toJson() => {
        'title': title,
        'value': value,
        'notes': notes,
      };

  @override
  String toString() {
    StringBuffer sb = StringBuffer()
      ..write('  ${styleBold.wrap(title)}: $value');
    if (notes != null) {
      sb..write(' ')..write(styleDim.wrap('($notes)'));
    }
    return sb.toString();
  }
}
