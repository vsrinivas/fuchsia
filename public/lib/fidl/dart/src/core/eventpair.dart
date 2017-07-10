// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class EventpairEndpoint {
  Handle handle;

  EventpairEndpoint(this.handle);

  void close() {
    handle.close();
    handle = null;
  }

  String toString() => 'EventpairEndpoint($handle)';
}

class Eventpair {
  Eventpair._({
    this.status: NO_ERROR,
    this.endpoint0,
    this.endpoint1,
  });

  factory Eventpair() {
    List result = MxEventpair.create(0);
    assert((result is List) && (result.length == 3));

    return new Eventpair._(
      status: result[0],
      endpoint0: new EventpairEndpoint(new Handle(result[1])),
      endpoint1: new EventpairEndpoint(new Handle(result[2])),
    );
  }

  final int status;
  EventpairEndpoint endpoint0;
  EventpairEndpoint endpoint1;

  EventpairEndpoint passEndpoint0() {
    final EventpairEndpoint result = endpoint0;
    endpoint0 = null;
    return result;
  }

  EventpairEndpoint passEndpoint1() {
    final EventpairEndpoint result = endpoint1;
    endpoint1 = null;
    return result;
  }
}
