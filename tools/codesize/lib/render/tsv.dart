// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:mirrors';

import '../common_util.dart';
import '../queries/index.dart';
import 'ast.dart';

/// A renderer into tab-separated values format. Since TSV is inherently 2D,
/// this renderer will truncate any nodes deeper than one level.
class TsvRenderer extends Renderer {
  @override
  void render(StringSink output, Iterable<Query> queries) {
    if (queries.length != 1) {
      throw Exception('TSV output only supports rendering a single query.\n'
          'Received: ${queries.map((q) => q.name).join(', ')}');
    }
    final result = queries.first.distill().export();
    _renderTsv(output, result);
  }

  /// Prints the first N items in `nodes` that are of the same type,
  /// using runtime reflection to query fields.
  void _renderTsv(StringSink output, Iterable<AnyNode> nodes) {
    // Query the concrete type `Node<T>` from `AnyNode`.
    final ClassMirror nodeType = reflect(nodes.first).type;
    final List<TypeMirror> typeArguments = nodeType.typeArguments;
    if (nodeType.simpleName != #Node ||
        typeArguments.length != 1 ||
        !(typeArguments[0] is ClassMirror)) {
      throw Exception('${nodes.first} should be of the form Node<T>.');
    }
    // `titleType` will be `SizeRecord`, `UniqueSymbolSizeRecord`, etc.
    final ClassMirror titleType = typeArguments[0];
    final fieldDescriptors = _buildFieldDescriptors(titleType);
    // Render TSV header
    output.writeln(
        fieldDescriptors.map((desc) => _ensureNoTab(desc.name)).join('\t'));
    for (final node in nodes) {
      // Stop when encountering a different type down the list, since a TSV
      // consists of N rows of homogenous types.
      if (reflect(node).type != nodeType) break;
      // Render one row
      final row = fieldDescriptors.map((desc) {
        Object fieldContent = desc.lens(node.title);
        return _ensureNoTab(_stripStyle(fieldContent));
      }).join('\t');
      output.writeln(row);
    }
  }

  /// Since TSV is un-styled plain text, this function strips any coloring from
  /// the text.
  String _stripStyle(Object field) {
    if (field is AddColor) {
      return field.details.map(_stripStyle).join('');
    } else if (field is StyledString) {
      return field.details.map(_stripStyle).join('');
    } else if (field is Plain) {
      return field.text;
    } else if (field is List) {
      return field.map(_stripStyle).join(' ');
    } else {
      return field.toString();
    }
  }

  /// TSV spec requires escaping `\t` (and consequently slashes as well).
  /// Since our symbols are unlikely to contain the tab character,
  /// simply asserting the output does not have tab should be good enough.
  String _ensureNoTab(String input) {
    if (input.contains('\t')) {
      throw Exception(
          'Escaping the tab character in TSV contents is not supported');
    }
    return input;
  }

  /// Given the mirror of class `type`, returns a list of field descriptors
  /// corresponding to columns of the TSV.
  List<_FieldDescriptor> _buildFieldDescriptors(ClassMirror type,
          {Object Function(Object root) lens = _id}) =>
      [
        // Superclass fields first...
        if (type.superclass != null)
          ..._buildFieldDescriptors(type.superclass, lens: lens),
        // Followed by our fields...
        for (final decl in type.declarations.entries)
          if (decl.value is VariableMirror)
            // Expand `Tally` into three fields (size, raw size, num symbols)
            // ignore: avoid_as
            if ((decl.value as VariableMirror).type.simpleName == #Tally)
              // Use a single-element for-loop to introduce a local `tallyLens`
              // name inside nested expressions.
              for (final tallyLens in [
                (x) => reflect(lens(x)).getField(decl.key).reflectee
              ]) ...[
                _FieldDescriptor(
                    name: 'Size', lens: (x) => formatSize(tallyLens(x).size)),
                _FieldDescriptor(
                    name: 'Raw Size', lens: (x) => tallyLens(x).size),
                _FieldDescriptor(
                    name: 'Num Symbols', lens: (x) => tallyLens(x).count),
              ]
            else
              // Note: we are assuming that all fields that is not `Tally` can
              // be directly printed through `_stripStyle` (e.g. StyledString,
              // int, String, etc). When some QueryReport start using classes,
              // change this to recursively descend into that class and build
              // the field descriptors there.
              _FieldDescriptor(
                  name: _formatTsvTitle(MirrorSystem.getName(decl.key)),
                  lens: (x) => reflect(lens(x)).getField(decl.key).reflectee)
      ];

  /// Convert `camelCase` into `Title Case`.
  String _formatTsvTitle(String name) {
    String upperCaseFirst(String x) => '${x[0].toUpperCase()}${x.substring(1)}';
    return upperCaseFirst(name).replaceAllMapped(
        _camelRegex, (Match m) => ' ${upperCaseFirst(m.group(0))}');
  }

  final RegExp _camelRegex = RegExp(r'(?<=[a-z])[A-Z]');

  static Object _id(Object x) => x;
}

/// The name of a field (TSV column), and an accessor to extract the value of
/// this field given an node object representing a row.
class _FieldDescriptor {
  final String name;
  final Object Function(Object root) lens;

  _FieldDescriptor({this.name, this.lens});
}
