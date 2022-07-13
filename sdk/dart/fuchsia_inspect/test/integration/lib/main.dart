// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.9

// ignore_for_file: implementation_imports, public_member_api_docs

import 'dart:typed_data';
import 'package:fuchsia_inspect/src/inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';

const String value = 'value';
int _uniqueValue = 0;
String uniqueName(String prefix) =>
    '${prefix}0x${(_uniqueValue++).toRadixString(16)}';

/// This example program exposes an Inspect VMO tree consisting of
/// [Table] nodes that can contain an arbitrary number of [Item] nodes.
/// [Item]s are stored in [Table]s. This is an example of a child node
/// with a parent.
class Item {
  final Node node;

  /// Constructs an Item.
  Item(this.node) {
    node.intProperty('value');
  }

  /// Adds [value] to the [Item]'s metric.
  void add(int value) => node.intProperty('value').add(value);
}

/// [Table]s can contain [Items]. This is an example of a parent
/// containing children.
class Table {
  final Node node;
  final List<Item> _items = [];

  /// Constructs a [Table].
  Table(this.node) {
    node
      ..intProperty('value').add(-10)
      ..byteDataProperty('frame').setValue(ByteData(3))
      ..stringProperty('version').setValue('1.0')
      ..boolProperty('active').setValue(true);
  }

  /// Adds an [Item] with value [value] to the [Table].
  Item newItem(int value) {
    var item = Item(node.child(uniqueName('item-')))..add(value);
    _items.add(item);
    return item;
  }
}

void main(List<String> args) {
  var context = ComponentContext.create();
  // ReadHierarchy Test
  var inspect = Inspect()..serve(context.outgoing);
  context.outgoing.serveFromStartupInfo();
  var t1 = Table(inspect.root.child('t1'));
  var t2 = Table(inspect.root.child('t2'));
  t1
    ..newItem(10)
    ..newItem(90).add(10);
  t2.newItem(2).add(2);
  // DynamicGeneratesNewHierarchy Test
  int nextDigit = 0;
  void writeNextDigit(Node root) {
    root.child('increments').stringProperty('value').setValue('$nextDigit');
    root.child('doubles').stringProperty('value').setValue('${nextDigit * 2}');
    nextDigit += 1;
  }

  inspect.onDemand('digits_of_numbers', writeNextDigit);
}
