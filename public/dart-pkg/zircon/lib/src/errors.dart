// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

class ZirconApiError extends Error {
  final String message;
  ZirconApiError(this.message) : super();

  @override
  String toString() => message;
}
