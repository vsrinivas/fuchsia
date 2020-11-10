// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:zircon/zircon.dart';

void closeHandles(List<Handle> handles) {
  for (final handle in handles) {
    handle.close();
  }
}

// Until Dart provides access to the zx_object_get_info syscall, check
// that the koid getter returns invalid to determine if the handle is valid
// (see https://github.com/flutter/engine/blob/e979c29a2a500189b5274b3cb20e3c55f1d53525/shell/platform/fuchsia/dart-pkg/zircon/sdk_ext/handle.h#L54)
bool isHandleClosed(Handle handle) => handle.koid == ZX.KOID_INVALID;

/// The handle subtypes that are supported by GIDL. This should match the
/// supportedHandleSubtypes defined in the GIDL IR.
enum HandleSubtype {
  event,
  channel,
}

// To support a new subtype in createHandles, add a new entry to this map.
const Map<HandleSubtype, List<Handle> Function(int numHandles)> _handleFactory =
    {
  HandleSubtype.event: _createEvents,
  HandleSubtype.channel: _createChannels,
};

/// Create a list of Handles `result` where `subtype of result[i] == subtypes[i]`
List<Handle> createHandles(List<HandleSubtype> subtypes) {
  // First, calculate the number of handles of each subtype that is needed
  Map<HandleSubtype, int> handleCounts = {};
  for (final subtype in subtypes) {
    handleCounts[subtype] = handleCounts.putIfAbsent(subtype, () => 0) + 1;
  }

  // Then, batch create the correct number of handles for each subtype. This
  // makes it possible to use both handles of a pair whenever possible.
  Map<HandleSubtype, List<Handle>> handles = Map.fromIterable(
    handleCounts.entries,
    key: (entry) => entry.key,
    value: (entry) => _handleFactory[entry.key](entry.value),
  );

  // Rearrange the created handles into the order specified by the input.
  List<Handle> result = [];
  for (final subtype in subtypes) {
    // reuse the handle counts as an index into the handles list
    result.add(handles[subtype][--handleCounts[subtype]]);
  }
  return result;
}

List<Handle> _createChannels(int numHandles) {
  final numPairs = (numHandles / 2).ceil();
  List<Handle> channels = [];
  for (var i = 0; i < numPairs; i++) {
    final pair = ChannelPair();
    assert(pair.status == ZX.OK, 'could not create test channel');
    channels.addAll([pair.first.handle, pair.second.handle]);
  }
  if (numHandles % 2 == 1) {
    channels.removeLast().close();
  }
  assert(channels.length == numHandles);
  return channels;
}

List<Handle> _createEvents(int numHandles) {
  // Use eventpairs since Dart does not have support for creating events.
  final numPairs = (numHandles / 2).ceil();
  List<Handle> events = [];
  for (var i = 0; i < numPairs; i++) {
    final pair = EventPairPair();
    assert(pair.status == ZX.OK, 'could not create test event');
    events.addAll([pair.first.handle, pair.second.handle]);
  }
  if (numHandles % 2 == 1) {
    events.removeLast().close();
  }
  assert(events.length == numHandles);
  return events;
}
