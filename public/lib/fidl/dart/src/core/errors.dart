// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class FidlInternalError {
  FidlInternalError(this._message);

  final String _message;

  String toString() => "FidlInternalError: $_message";
}

class FidlApiError {
  FidlApiError(this._message);

  final String _message;

  String toString() => "FidlApiError: $_message";
}
