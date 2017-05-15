// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/composer/resources/link.h"

namespace mozart {
namespace composer {

class Session;

// Interface that describes the ways that a |Session| communicates with its
// environment.
class SessionContext {
 public:
  SessionContext() = default;
  virtual ~SessionContext() = default;

  virtual LinkPtr CreateLink(Session* session,
                             const mozart2::LinkPtr& args) = 0;

  virtual void OnSessionTearDown(Session* session) = 0;
};

}  // namespace composer
}  // namespace mozart
