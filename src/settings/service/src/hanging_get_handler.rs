// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/79048): Cleanup the remaining usages of Sender or move it to a
// more appropriate location.
/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    fn send_response(self, data: T);
    fn on_error(self, error: &anyhow::Error);
}
