// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// These values are from these headers:
//
//  * https://fuchsia.googlesource.com/magenta/+/master/system/public/magenta/errors.h
//  * https://fuchsia.googlesource.com/magenta/+/master/system/public/magenta/types.h

// System.socketCreate options
const int MX_SOCKET_STREAM = 0; // ignore: constant_identifier_names
const int MX_SOCKET_DATAGRAM = 1; // ignore: constant_identifier_names

// TODO(ianloic): move constants from core/types.dart
