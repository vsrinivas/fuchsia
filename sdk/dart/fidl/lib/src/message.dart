// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:zircon/zircon.dart';

import '../fidl.dart';
import 'codec.dart';
import 'error.dart';
import 'types.dart';
// ignore_for_file: public_member_api_docs

const int kMessageHeaderSize = 16;
const int kMessageTxidOffset = 0;
const int kMessageFlagOffset = 4;
const int kMessageMagicOffset = 7;
const int kMessageOrdinalOffset = 8;

const int kMagicNumberInitial = 1;

class Message {
  Message(this.data, this.handles);
  Message.fromReadResult(ReadResult result)
      : data = result.bytes,
        handles = result.handles,
        assert(result.status == ZX.OK);

  final ByteData data;
  final List<Handle> handles;

  int get txid => data.getUint32(kMessageTxidOffset, Endian.little);
  set txid(int value) =>
      data.setUint32(kMessageTxidOffset, value, Endian.little);

  int get ordinal => data.getUint64(kMessageOrdinalOffset, Endian.little);
  int get magic => data.getUint8(kMessageMagicOffset);

  bool isCompatible() => magic == kMagicNumberInitial;

  void hexDump() {
    const int width = 16;
    Uint8List list = Uint8List.view(data.buffer, 0);
    StringBuffer buffer = StringBuffer();
    final RegExp isPrintable = RegExp(r'\w');
    for (int i = 0; i < data.lengthInBytes; i += width) {
      StringBuffer hex = StringBuffer();
      StringBuffer printable = StringBuffer();
      for (int j = 0; j < width && i + j < data.lengthInBytes; j++) {
        int v = list[i + j];
        String s = v.toRadixString(16);
        if (s.length == 1)
          hex.write('0$s ');
        else
          hex.write('$s ');

        s = String.fromCharCode(v);
        if (isPrintable.hasMatch(s)) {
          printable.write(s);
        } else {
          printable.write('.');
        }
      }
      buffer.write('${hex.toString().padRight(3 * width)} $printable\n');
    }

    print('==================================================\n'
        '$buffer'
        '==================================================');
  }

  void closeHandles() {
    for (int i = 0; i < handles.length; ++i) {
      handles[i].close();
    }
  }

  @override
  String toString() {
    return 'Message(numBytes=${data.lengthInBytes}, numHandles=${handles.length})';
  }
}

void _validateDecoding(Decoder decoder) {
  if (decoder.countUnclaimedHandles() > 0) {
    // If there are unclaimed handles at the end of the decoding, close all
    // handles to the best of our ability, and throw an error.
    for (var handle in decoder.handles) {
      try {
        handle.close();
        // ignore: avoid_catches_without_on_clauses
      } catch (e) {
        // best effort
      }
    }

    int unclaimed = decoder.countUnclaimedHandles();
    int total = decoder.handles.length;
    throw FidlError(
        'Message contains extra handles (unclaimed: $unclaimed, total: $total)',
        FidlErrorCode.fidlTooManyHandles);
  }
}

/// Decodes a single FIDL message.  Most messages can be decoded using this
/// entry point.  Such a messages generally take the form of header bytes
/// followed immediately by the encoded message bytes.
T decodeMessage<T>(Message message, int inlineSize, MemberType typ) {
  final Decoder decoder = Decoder(message)
    ..claimMemory(kMessageHeaderSize + inlineSize);
  T decoded = typ.decode(decoder, kMessageHeaderSize);
  _validateDecoding(decoder);
  return decoded;
}

/// Decodes a FIDL message with multiple parameters.  Such messages generally
/// take the form of header bytes followed by the encoded bytes of each
/// parameter in turn.  The generated bindings should then wrap the resulting
/// parameter types into a single "Async___Class" inside the callback function.
A decodeMessageWithCallback<A>(
    Message message, int inlineSize, A Function(Decoder decoder) f) {
  final Decoder decoder = Decoder(message)
    ..claimMemory(kMessageHeaderSize + inlineSize);
  A out = f(decoder);
  _validateDecoding(decoder);
  return out;
}

typedef MessageSink = void Function(Message message);
