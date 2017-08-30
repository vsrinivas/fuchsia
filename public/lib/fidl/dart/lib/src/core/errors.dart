// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class FidlApiError extends Error {
  final String message;
  FidlApiError(this.message) : super();

  @override
  String toString() => message;
}
