/// Copyright 2019 The Fuchsia Authors. All rights reserved.
/// Use of this source code is governed by a BSD-style license that can be
/// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:flutter/foundation.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_inspect/src/inspect/internal/_inspect_impl.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_writer.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:fuchsia_inspect_flutter/src/inspect_flutter.dart';
import 'package:test/test.dart';

// This class was made to test the InspectFlutter class
// The FakeDiagnosticsNode allows properties and children
// to be added to the node.
class FakeDiagnosticsNode extends DiagnosticsNode {
  List<FakeDiagnosticsNode> properties = <FakeDiagnosticsNode>[];
  List<FakeDiagnosticsNode> children = <FakeDiagnosticsNode>[];
  @override
  String? value;

  FakeDiagnosticsNode(String? newName)
      : super(name: newName, style: DiagnosticsTreeStyle.dense);

  @override
  List<FakeDiagnosticsNode> getChildren() {
    return children;
  }

  @override
  List<FakeDiagnosticsNode> getProperties() {
    return properties;
  }

  void addProperty(String? propertyName, String? propertyValue) {
    var fakeNode = (FakeDiagnosticsNode(propertyName)..value = propertyValue);
    properties.add(fakeNode);
  }

  void addChild(String childName, String childValue) {
    var fakeNode = (FakeDiagnosticsNode(childName)..value = childValue);
    children.add(fakeNode);
  }

  @override
  String toDescription({TextTreeConfiguration? parentConfiguration}) {
    return '$value';
  }
}

void main() {
  late VmoHolder vmo;
  Node? root;
  const defaultVmoSize = 256 * 1024;

  setUp(() {
    vmo = FakeVmoHolder(defaultVmoSize);
    var writer = VmoWriter.withVmo(vmo);
    Inspect inspect = InspectImpl(writer);
    root = inspect.root;
  });

  test('Widget Tree Output is correct', () {
    FakeDiagnosticsNode fakeNode = (FakeDiagnosticsNode('IGNORED')
      ..addProperty('widget', 'node1')
      ..addProperty('prop1', 'value1')
      ..addProperty('prop2', 'value2')
      ..addProperty('prop3', 'value3')
      ..addChild('widget', 'node2')
      ..children[0].addProperty('widget', 'node2'));
    InspectFlutter.inspectFromDiagnostic(fakeNode, root);
    expect(
        VmoMatcher(vmo).node().at(['node1_${fakeNode.hashCode}'])
          ..propertyEquals('prop1', 'value1')
          ..propertyEquals('prop2', 'value2')
          ..propertyEquals('prop3', 'value3')
          ..propertyEquals('widget', 'node1'),
        hasNoErrors);
    expect(
        VmoMatcher(vmo).node().at([
          'node1_${fakeNode.hashCode}',
          'node2_${fakeNode.children[0].hashCode}'
        ])
          ..propertyEquals('widget', 'node2'),
        hasNoErrors);
  });

  test('Widget Tree Output does not display null properties', () {
    FakeDiagnosticsNode fakeNode = (FakeDiagnosticsNode('IGNORED')
      ..addProperty('widget', 'node1')
      ..addProperty('prop1', 'value1')
      ..addProperty('prop2', 'value2')
      ..addProperty('prop3', 'value3')
      ..addProperty(null, null));
    InspectFlutter.inspectFromDiagnostic(fakeNode, root);
    expect(
        VmoMatcher(vmo).node().at(['node1_${fakeNode.hashCode}'])
          ..propertyEquals('prop1', 'value1')
          ..propertyEquals('prop2', 'value2')
          ..propertyEquals('prop3', 'value3')
          ..propertyEquals('widget', 'node1'),
        hasNoErrors);
  });

  test('Widget Tree Output does not display a node with no widget property',
      () {
    FakeDiagnosticsNode fakeNode = (FakeDiagnosticsNode('IGNORED')
      ..addProperty('prop1', 'value1')
      ..addProperty('prop2', 'value2')
      ..addProperty('prop3', 'value3'));
    InspectFlutter.inspectFromDiagnostic(fakeNode, root);
    expect(VmoMatcher(vmo).node()..missingChild('node1_${fakeNode.hashCode}'),
        hasNoErrors);
  });
}
