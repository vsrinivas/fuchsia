// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of bindings;

/// An enumerated typed in a FIDL interface.
class FidlEnum {
  /// The numerical value assigned to this value in the interface.
  final int fidlEnumValue;

  /// Creates an instance with the given numerical value.
  const FidlEnum(this.fidlEnumValue);
}
