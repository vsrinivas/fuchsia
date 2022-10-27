// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide OAuth2 support for Google Cloud Storage (GCS) access.
//!
//! There are two main APIs here:
//! - new_refresh_token() gets a long-lived, storable (to disk) token than can
//!                       be used to create new access tokens.
//! - new_access_token() accepts a refresh token and returns a reusable though
//!                      short-lived token that can be used to access data.
//!
//! Caution: Some data handled here are security access keys (tokens) and must
//!          not be logged (or traced) or otherwise put someplace where the
//!          secrecy could be compromised. I.e. watch out when adding/reviewing
//!          log::*, tracing::*, or `impl` of Display or Debug.

pub mod device;
pub mod info;
pub mod oob;
pub mod pkce;
