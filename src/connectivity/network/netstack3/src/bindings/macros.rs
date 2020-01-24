// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Errors from `$responder.send` can be safely ignored during regular operation;
// they are handled only by logging to debug.
macro_rules! responder_send {
    ($responder:expr, $arg:expr) => {
        $responder.send($arg).unwrap_or_else(|e| debug!("Responder send error: {:?}", e));
    };
    ($responder:expr, $arg1:expr, $arg2:expr) => {
        $responder.send($arg1, $arg2).unwrap_or_else(|e| debug!("Responder send error: {:?}", e));
    };
}
