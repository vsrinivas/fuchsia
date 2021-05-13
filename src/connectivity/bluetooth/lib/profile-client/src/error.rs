// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_bluetooth_bredr as bredr, thiserror::Error};

#[derive(Error, Debug)]
pub enum Error {
    /// An attempt to add a search to an already terminated `ProfileClient`.
    #[error("Error adding search to already terminated ProfileClient")]
    AlreadyTerminated,
    /// A search result stream produced an error.
    #[error("Search Result stream ended in error: {:?}", source)]
    SearchResult { source: Box<dyn std::error::Error + Send + Sync> },
    /// A connection receiver finished with an error.
    #[error("Connection Receiver stream ended in error: {:?}", source)]
    ConnectionReceiver { source: Box<dyn std::error::Error + Send + Sync> },
    /// The services advertised were completed unexpectedly. `result` contains the response from
    /// the profile advertise call.
    #[error("Advertisement ended prematurely: {:?}", result)]
    Advertisement { result: bredr::ProfileAdvertiseResult },
    /// Another FIDL error has occurred.
    #[error("FIDL Error occurred: {:?}", source)]
    Fidl { source: fidl::Error },
}

impl Error {
    pub fn connection_receiver<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::ConnectionReceiver { source: e.into() }
    }

    pub fn search_result<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::SearchResult { source: e.into() }
    }
}

impl From<fidl::Error> for Error {
    fn from(source: fidl::Error) -> Self {
        Self::Fidl { source }
    }
}
