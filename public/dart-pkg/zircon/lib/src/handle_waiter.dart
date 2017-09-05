// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// ignore_for_file: native_function_body_in_non_sdk_code

typedef void AsyncWaitCallback(int status, int pending);

class HandleWaiter extends NativeFieldWrapperClass2 {
  // Private constructor.
  HandleWaiter._();

  void cancel() native "HandleWaiter_Cancel";
}
