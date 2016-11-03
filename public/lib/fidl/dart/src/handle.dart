// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class Handle {
  // TODO(floitsch): get the INVALID value from the backing internal
  // implementation.
  static const int INVALID = 0;
  static const int DEADLINE_INDEFINITE = -1;

  // The type of this field is determined by the backing internal
  // implementation.
  Object _h;
  Object get h => _h;

  Handle(this._h, {String description}) {
    HandleNatives.addOpenHandle(_h, description: description);
  }

  Handle._internal(this._h);

  Handle.invalid() : this._internal(INVALID);

  int close() {
    HandleNatives.removeOpenHandle(_h);
    int result = HandleNatives.close(_h);
    _h = INVALID;
    return result;
  }

  Handle pass() {
    HandleNatives.removeOpenHandle(_h);
    return this;
  }

  MojoWaitResult wait(int signals, int deadline) {
    List result = HandleNatives.wait(h, signals, deadline);
    var state = result[1] != null
        ? new HandleSignalsState(result[1][0], result[1][1])
        : null;
    return new MojoWaitResult(result[0], state);
  }

  bool _ready(int signal) {
    MojoWaitResult mwr = wait(signal, 0);
    switch (mwr.result) {
      case MojoResult.kOk:
        return true;
      case MojoResult.kDeadlineExceeded:
      case MojoResult.kCancelled:
      case MojoResult.kInvalidArgument:
      case MojoResult.kFailedPrecondition:
        return false;
      default:
        // Should be unreachable.
        throw new FidlInternalError("Unexpected result $mwr for wait on $h");
    }
  }

  bool get readyRead => _ready(HandleSignals.kPeerClosedReadable);
  bool get readyWrite => _ready(HandleSignals.kWritable);
  bool get isValid => (_h != INVALID);

  String toString() {
    if (!isValid) {
      return "Handle(INVALID)";
    }
    var mwr = wait(HandleSignals.kAll, 0);
    return "Handle(h: $h, status: $mwr)";
  }

  bool operator ==(other) =>
      (other is Handle) && (_h == other._h);

  int get hashCode => _h.hashCode;

  static MojoWaitManyResult waitMany(
      List<int> handles, List<int> signals, int deadline) {
    List result = HandleNatives.waitMany(handles, signals, deadline);
    List states = result[2] != null
        ? result[2].map((l) => new HandleSignalsState(l[0], l[1])).toList()
        : null;
    return new MojoWaitManyResult(result[0], result[1], states);
  }

  static bool registerFinalizer(MojoEventSubscription eventSubscription) {
    return HandleNatives.registerFinalizer(
            eventSubscription, eventSubscription._handle.h) ==
        MojoResult.kOk;
  }

  static bool reportLeakedHandles() => HandleNatives.reportOpenHandles();
}
