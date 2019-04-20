// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_SETTINGS_H_
#define SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_SETTINGS_H_

namespace google_auth_provider {

struct Settings {
  // Set true to request the "GLIF" UI style. The default of false will request
  // the legacy "RedCarpet" UI style.
  bool use_glif = false;
  // Set true to connect to a dedicated authentication endpoint for Fuchsia
  // instead of the standard OAuth endpoint.
  bool use_dedicated_endpoint = false;
};

}  // namespace google_auth_provider

#endif  // SRC_IDENTITY_BIN_GOOGLE_AUTH_PROVIDER_SETTINGS_H_
