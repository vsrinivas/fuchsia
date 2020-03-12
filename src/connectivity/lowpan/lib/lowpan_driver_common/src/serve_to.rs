// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Trait for serving an object to a given FIDL request stream.
#[async_trait::async_trait(?Send)]
pub trait ServeTo<RS: fidl::endpoints::RequestStream> {
    /// Returns a future which handles requests from the given request stream.
    async fn serve_to(&self, request_stream: RS) -> anyhow::Result<()>;
}
