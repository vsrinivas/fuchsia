// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: invalid_use_of_visible_for_testing_member, implementation_imports

import 'dart:convert' show utf8;
import 'dart:math' show min, max;
import 'dart:typed_data';

import 'package:fuchsia_inspect/src/inspect/inspect.dart';
import 'package:fuchsia_inspect/src/vmo/block.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_fields.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_writer.dart';

String _utf8ToString(ByteData bytes) {
  return utf8.decode(bytes.buffer
      .asUint8ClampedList(bytes.offsetInBytes, bytes.lengthInBytes));
}

class _Metric {
  num value;

  _INode parent;

  String _name;

  String get name => _name;

  BlockType _type;

  BlockType get type => _type;

  _Metric(Block metric) {
    var nameBlock = Block.read(metric.vmo, metric.nameIndex);
    _name = _utf8ToString(nameBlock.nameUtf8);
    _type = metric.type;
    switch (metric.type) {
      case BlockType.doubleValue:
        value = metric.doubleValue;
        break;
      case BlockType.intValue:
        value = metric.intValue;
        break;
      case BlockType.uintValue:
        throw StateError('Dart does not expect uint metrics.');
      default:
        throw StateError('Type was ${metric.type.name} not numeric.');
    }
  }

  @override
  String toString() =>
      '${_type == BlockType.intValue ? 'Int' : 'Double'}Property "$_name": $value';
}

class _Property {
  _INode parent;

  String _name;

  String get name => _name;

  final int bytesToPrint = 20;

  bool isString;

  int payloadLength;

  ByteData bytes;

  _Property(Block property) {
    var nameBlock = Block.read(property.vmo, property.nameIndex);
    _name = _utf8ToString(nameBlock.nameUtf8);
    isString = property.bufferFlags == propertyUtf8Flag;
    payloadLength = property.bufferTotalLength;
    bytes = ByteData(payloadLength);
    int amountCopied = 0;
    for (Block extentBlock =
            Block.read(property.vmo, property.bufferExtentIndex);;
        extentBlock = Block.read(property.vmo, extentBlock.nextExtent)) {
      int copyEnd =
          min(payloadLength, amountCopied + extentBlock.payloadSpaceBytes);
      bytes.buffer.asUint8List().setRange(
          amountCopied, copyEnd, extentBlock.payloadBytes.buffer.asUint8List());
      if (extentBlock.nextExtent == invalidIndex) {
        break;
      }
    }
  }

  @override
  String toString() {
    if (isString) {
      return 'StringProperty "$name": "${_utf8ToString(bytes)}"';
    } else {
      var buffer = StringBuffer('ByteDataProperty  "$name":');
      for (int i = 0; i < bytesToPrint && i < payloadLength; i++) {
        buffer.write(' ${bytes.getUint8(i).toRadixString(16).padLeft(2, '0')}');
      }
      return buffer.toString();
    }
  }
}

class _INode {
  _INode parent;

  List<_INode> children = <_INode>[];

  List<_Metric> metrics = <_Metric>[];

  List<_Property> properties = <_Property>[];

  String _name;

  String get name => _name;

  void setFrom(Block block) {
    var nameBlock = Block.read(block.vmo, block.nameIndex);
    _name = _utf8ToString(nameBlock.nameUtf8);
  }

  @override
  String toString() => 'Node: "$_name"';

  String treeToString(String indentStep, [String currentIndent = '']) {
    var buffer = StringBuffer()
      ..write(currentIndent)
      ..writeln(this);
    String nextIndent = '$currentIndent$indentStep';
    for (var metric in metrics) {
      buffer
        ..write(nextIndent)
        ..writeln(metric);
    }
    for (var property in properties) {
      buffer
        ..write(nextIndent)
        ..writeln(property);
    }
    for (var node in children) {
      buffer.write(node.treeToString(indentStep, nextIndent));
    }
    return buffer.toString();
  }
}

class _NodeTree {
  final _nodes = <int, _INode>{};
  final VmoHolder vmo;

  _NodeTree(this.vmo);

  _INode node(int index) {
    if (!_nodes.containsKey(index)) {
      _nodes[index] = _INode();
    }
    return _nodes[index];
  }
}

class VmoReader {
  final _NodeTree _nodes;

  VmoReader(VmoHolder vmo) : _nodes = _NodeTree(vmo) {
    for (int readPosBytes = 0; readPosBytes < vmo.size;) {
      var block = Block.read(vmo, readPosBytes ~/ bytesPerIndex);
      switch (block.type) {
        case BlockType.nodeValue:
          _INode parentNode = _nodes.node(block.parentIndex);
          _INode node = _nodes.node(block.index)
            ..setFrom(block)
            ..parent = parentNode;
          parentNode.children.add(node);
          break;
        case BlockType.bufferValue:
          _INode parentNode = _nodes.node(block.parentIndex);
          var property = _Property(block);
          property.parent = parentNode;
          parentNode.properties.add(property);
          break;
        case BlockType.intValue:
        case BlockType.doubleValue:
          _INode parentNode = _nodes.node(block.parentIndex);
          var metric = _Metric(block);
          metric.parent = parentNode;
          parentNode.metrics.add(metric);
          break;
      }
      readPosBytes += max(block.size, 16);
    }
  }

  _INode get _rootNode => _nodes.node(rootNodeIndex);

  @override
  String toString() => _rootNode.treeToString('>> ', '<> ');
}
