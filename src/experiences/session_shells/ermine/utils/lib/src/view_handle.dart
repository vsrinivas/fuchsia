// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


import 'package:fidl_fuchsia_ui_views/fidl_async.dart';

/// A helper class that wraps [ViewRef] and provides convenient accessors.
class ViewHandle {
  final ViewRef viewRef;
  ViewHandle(this.viewRef);

  /// Two [ViewHandle] objects are equal if their [koid]s match.
  @override
  bool operator ==(Object other) {
    return other is ViewHandle && koid == other.koid ||
        other is ViewRef && koid == other.reference.handle?.koid;
  }

  @override
  int get hashCode => koid.hashCode;

  @override
  String toString() => 'ViewRef[koid: $koid]';

  int get koid => viewRef.reference.handle?.koid ?? -1;

  int get handle => viewRef.reference.handle?.handle ?? -1;

}
