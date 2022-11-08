// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!
//! Request that the user open a browser to this location:
//!   - gcs::token_store::REFRESH_TOKEN_URL
//! The user should then authenticate and grant permission to this program to
//! use GCS. Upon approval, the web page will present a long string to
//! copy-paste, this is the `refresh_token`.
//!
//! Ask the user to paste the token back into your application. Then use that
//! token to create a TokenStore.
//!
//! The token_store may then be used to create a ClientFactory.
//!
//! E.g.
//! ```
//! let refresh_token = new_refresh_token();
//! let token_store = TokenStore::new(pasted_token);
//! let client_factory = ClientFactory::new_with_auth(token_store);
//! let client = client_factory.create_client();
//! let res = client.download("some-bucket", "some-object").await?;
//! if res.status() == StatusCode::OK {
//!     let stdout = io::stdout();
//!     let mut handle = stdout.lock();
//!     while let Some(next) = res.data().await {
//!         let chunk = next.expect("next chunk");
//!         handle.write_all(&chunk).expect("write chunk");
//!     }
//! }
//! ```

pub mod auth;
pub mod client;
pub mod error;
pub mod gs_url;
pub mod mock_https_client;
pub mod token_store;
