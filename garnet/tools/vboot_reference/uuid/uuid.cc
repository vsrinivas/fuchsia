// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uuid/uuid.h"

#include <openssl/rand.h>

void uuid_generate(uint8_t out[16]) { RAND_bytes((unsigned char*)out, 16); }
