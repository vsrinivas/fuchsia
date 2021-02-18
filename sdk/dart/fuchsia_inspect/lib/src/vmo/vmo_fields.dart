// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Names and numbers from the VMO spec.
///
/// Defines BlockType enum.
///
/// Defines BitRange's for all the header and payload fields.

// ignore_for_file: prefer_constructors_over_static_methods

import 'bitfield64.dart';

/// Index of the one-and-only header block (16 bytes).
const int headerIndex = 0;

/// 'INSP' utf8 string (little endian) for HEADER block magic value.
const int headerMagicNumber = 0x50534e49;

/// Each increment of [index] is 16 bytes in the VMO.
const int bytesPerIndex = 16;

/// Version for HEADER block.
const int headerVersionNumber = 1;

/// First index available for the heap.
const int heapStartIndex = 2;

/// Size of VMO-block's header bitfield in bytes.
const int headerSizeBytes = 8;

/// Flag for utf8 Property.
const int propertyUtf8Flag = 0;

/// Flag for binary Property.
const int propertyBinaryFlag = 1;

/// Types of VMO blocks.
///
/// Basically an enum with conversion to/from specified numeric values.
class BlockType {
  /// Gets the numeric [value] of this member.
  final int value;

  /// Printable [name] of the element.
  final String name;

  /// Contains all elements.
  static const List<BlockType> values = [
    free,
    reserved,
    header,
    nodeValue,
    intValue,
    uintValue,
    doubleValue,
    bufferValue,
    extent,
    nameUtf8,
    tombstone,
    anyValue,
    blockType12NotImplemented, // Dummy Value because not all header types are implemented.
    boolValue
  ];

  @override
  String toString() => name;

  const BlockType._(this.value, this.name);

  /// Empty block, ready to be used.
  static const BlockType free = BlockType._(0, 'free');

  /// In transition toward being used.
  static const BlockType reserved = BlockType._(1, 'reserved');

  /// One block to rule them all. Index 0.
  static const BlockType header = BlockType._(2, 'header');

  /// An entry in the Inspect tree, which may hold child Values: Nodes,
  /// Metrics, or Properties.
  static const BlockType nodeValue = BlockType._(3, 'nodeValue');

  /// An int Metric.
  static const BlockType intValue = BlockType._(4, 'intValue');

  /// A uint Metric.
  static const BlockType uintValue = BlockType._(5, 'uintValue');

  /// A double Metric.
  static const BlockType doubleValue = BlockType._(6, 'doubleValue');

  /// The header of a string or byte-vector Property.
  static const BlockType bufferValue = BlockType._(7, 'bufferValue');

  /// The contents of a string Property (in a singly linked list, if necessary).
  static const BlockType extent = BlockType._(8, 'extent');

  /// The name of a Value (Property, Metric, or Node) stored as utf8.
  ///
  /// Name must be contained in this one block. This may truncate utf8 strings
  /// in the middle of a multibyte character.
  static const BlockType nameUtf8 = BlockType._(9, 'nameUtf8');

  /// A property that's been deleted but still has live children.
  static const BlockType tombstone = BlockType._(10, 'tombstone');

  /// *_VALUE type, for internal use.
  ///
  /// Not valid if written to VMO.
  static const BlockType anyValue = BlockType._(11, 'anyValue');

  /// Dummy block type because things aren't implemented
  static const BlockType blockType12NotImplemented =
      BlockType._(12, 'blockType12NotImplemented');

  /// A bool Metric.
  static const BlockType boolValue = BlockType._(13, 'boolValue');
}

/// Order defines the block size: 1 << (order + 4).
final BitRange orderBits = BitRange(0, 3);

/// Type is one of the BlockType values.
final BitRange typeBits = BitRange(8, 15);

/// Version field of HEADER-type blocks.
final BitRange headerVersionBits = BitRange(16, 31);

/// "Magic" field of HEADER-type blocks.
final BitRange headerMagicBits = BitRange(32, 63);

/// NextFreeBlock field of FREE-type blocks.
final BitRange nextFreeBits = BitRange(16, 39);

/// Parent Index field of *_VALUE blocks.
final BitRange parentIndexBits = BitRange(16, 39);

/// Name Index field of *_VALUE blocks.
final BitRange nameIndexBits = BitRange(40, 63);

/// Total Length field of BUFFER_VALUE blocks payload bits.
final BitRange bufferTotalLengthBits = BitRange(0, 31);

/// Extent Index field of BUFFER_VALUE blocks payload bits.
final BitRange bufferExtentIndexBits = BitRange(32, 59);

/// Flags field of BUFFER_VALUE blocks payload bits.
final BitRange bufferFlagsBits = BitRange(60, 63);

/// Next Extent field of EXTENT blocks.
final BitRange nextExtentBits = BitRange(16, 39);

/// Length field of NAME blocks.
final BitRange nameLengthBits = BitRange(16, 27);
