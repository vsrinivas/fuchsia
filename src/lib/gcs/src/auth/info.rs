// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Information on client data.

/// For a web site, a client secret is kept locked away in a secure server. This
/// is not a web site and the value is needed, so a non-secret "secret" is used.
///
/// These values (and the quote following) are taken form
/// https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/c6a2ee693093926868170f678d8d290bf0de0c15/third_party/gsutil/oauth2_plugin/oauth2_helper.py
/// and
/// https://chromium.googlesource.com/catapult/+/2c541cdf008959bc9813c641cc4ecd0194979486/third_party/gsutil/gslib/utils/system_util.py#177
///
/// "Google OAuth2 clients always have a secret, even if the client is an
/// installed application/utility such as gsutil.  Of course, in such cases the
/// "secret" is actually publicly known; security depends entirely on the
/// secrecy of refresh tokens, which effectively become bearer tokens."
pub(crate) const CLIENT_ID: &str = "909320924072.apps.googleusercontent.com";
pub(crate) const CLIENT_SECRET: &str = "p3RlpR10xMFh9ZXBS/ZNLYUu";
