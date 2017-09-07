// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

// TODO: can this class be retired in favour of HandlePairResult?
class Eventpair {
  factory Eventpair() {
    HandlePairResult result = System.eventpairCreate();

    return new Eventpair._(
      status: result.status,
      endpoint0: result.first,
      endpoint1: result.second,
    );
  }

  Eventpair._({
    this.status: ZX.OK,
    this.endpoint0,
    this.endpoint1,
  });

  final int status;
  Handle endpoint0;
  Handle endpoint1;

  Handle passEndpoint0() {
    final Handle result = endpoint0;
    endpoint0 = null;
    return result;
  }

  Handle passEndpoint1() {
    final Handle result = endpoint1;
    endpoint1 = null;
    return result;
  }
}
