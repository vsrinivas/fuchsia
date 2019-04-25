// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_QUERY_READ_H_
#define LIB_INSPECT_QUERY_READ_H_

#include "source.h"

namespace inspect {

// Consults the file system to interpret and open the given location, reading
// it into a new Source.
fit::promise<Source, std::string> ReadLocation(Location location,
                                               int depth = -1);

}  // namespace inspect

#endif  // LIB_INSPECT_QUERY_READ_H_
