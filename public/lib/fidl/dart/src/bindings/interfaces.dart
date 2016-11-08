// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of bindings;

/// [FidlInterface] is the interface implemented by the generated
/// Interface and InterfaceRequest classes. The [FidlInterfaceControl] field
/// [ctrl] gives access to the underlying implementation of the interface, which
/// is either a [ProxyControl] or [StubControl] depending on the situation.
abstract class FidlInterface<T> {
  FidlInterfaceControl get ctrl;
  T impl;
  Future close({bool immediate: false});
}

/// This interface is implemented by [ProxyMessageHandler] and
/// [StubMessageHandler]. Most of the interface is inherited from, and
/// ultimately implemented by [core.FidlEventHandler].
abstract class FidlInterfaceControl implements core.FidlEventHandler {
  String get serviceName;
  int get version;
}
