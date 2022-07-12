// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: public_member_api_docs

import 'types.dart';

// TODO(fxbug.dev/8076) Generate these values.
enum FidlErrorCode {
  unknown,
  fidlExceededMaxOutOfLineDepth,
  fidlInvalidBoolean,
  fidlInvalidPresenceIndicator,
  fidlInvalidNumBytesInEnvelope,
  fidlInvalidNumHandlesInEnvelope,
  fidlInvalidInlineMarkerInEnvelope,
  fidlTooFewBytes,
  fidlTooManyBytes,
  fidlTooFewHandles,
  fidlTooManyHandles,
  fidlStringTooLong,
  fidlNonNullableTypeWithNullValue,
  fidlStrictUnionUnknownField,
  fidlUnknownMagic,
  fidlInvalidBit,
  fidlInvalidEnumValue,
  fidlIntOutOfRange,
  fidlNonEmptyStringWithNullBody,
  fidlNonEmptyVectorWithNullBody,
  fidlNonResourceHandle,
  fidlMissingRequiredHandleRights,
  fidlIncorrectHandleType,
  fidlInvalidInlineBitInEnvelope,
  fidlCountExceedsLimit,
  fidlInvalidPaddingByte,
  fidlUnknownMethod,
}

class FidlError implements Exception {
  // TODO(fxbug.dev/7865) Make code a required parameter.
  const FidlError(this.message, [this.code = FidlErrorCode.unknown]);

  factory FidlError.fromTransportErr(TransportErr transportErr) {
    switch (transportErr) {
      case TransportErr.unknownMethod:
        return const UnknownMethodException();
    }
  }

  final String message;
  final FidlErrorCode code;

  @override
  String toString() => 'FidlError: $message';
}

class FidlRangeCheckError implements FidlError {
  FidlRangeCheckError(this.value, this.min, this.max);

  final int value;
  final int min;
  final int max;
  @override
  String get message => 'FidlRangeCheckError: $value < $min or $value > $max';
  @override
  FidlErrorCode get code => FidlErrorCode.fidlIntOutOfRange;
}

/// If a FIDL method defines an application-level error, this exception will be
/// thrown with the error as its value.
class MethodException<T> implements Exception {
  MethodException(this.value);

  final T value;

  @override
  String toString() => 'MethodException: $value';
}

class UnknownMethodException extends FidlError {
  const UnknownMethodException()
      : super('UnknownMethodException', FidlErrorCode.fidlUnknownMethod);
}
