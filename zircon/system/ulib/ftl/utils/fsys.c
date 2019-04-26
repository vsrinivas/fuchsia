// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ftl_private.h>
#include <kprivate/fsprivate.h>

// Lookup for number of bits in half byte.
const ui8 NumberOnes[] = {
    //0 1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
};

