// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

class FidlInternalError {
  final String _message;

  FidlInternalError(this._message);

  String toString() => "FidlInternalError: $_msg";
}

class FidlApiError {
  final String _message;

  FidlApiError(this._message);

  String toString() => "FidlApiError: $_msg";
}

/// The object passed to the handler's error handling function containing both
/// the thrown error and the associated stack trace.
class FidlEventHandlerError {
  final Object error;
  final StackTrace stacktrace;

  FidlEventHandlerError(this.error, this.stacktrace);

  @override
  String toString() => error.toString();
}
