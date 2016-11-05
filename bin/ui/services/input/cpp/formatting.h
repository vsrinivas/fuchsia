// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_
#define APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_

#include <iosfwd>

#include "apps/mozart/services/input/input_event_constants.fidl.h"
#include "apps/mozart/services/input/input_events.fidl.h"

namespace mozart {

std::ostream& operator<<(std::ostream& os, const KeyData& value);
std::ostream& operator<<(std::ostream& os, const PointerData& value);
std::ostream& operator<<(std::ostream& os, const Event& value);

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_INPUT_CPP_FORMATTING_H_
