// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_TRANSFORMER_CONFORMANCE_UTILS_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_TRANSFORMER_CONFORMANCE_UTILS_H_

#include <lib/fidl/transformer.h>

#include "zircon/types.h"

__BEGIN_CDECLS

// Runs `fidl_transform` but does not check anything. This is used in failure
// tests to make sure the transformer does not crash on invalid inputs.
void run_fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                        const uint8_t* src_bytes, uint32_t src_num_bytes);

// Asserts that `fidl_transform` succeeds and produces the expected bytes.
bool check_fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                          const uint8_t* src_bytes, uint32_t src_num_bytes,
                          const uint8_t* expected_bytes, uint32_t expected_num_bytes);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_TRANSFORMER_CONFORMANCE_UTILS_H_
