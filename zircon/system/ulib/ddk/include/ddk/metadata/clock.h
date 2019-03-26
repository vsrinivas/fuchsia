// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_CLOCK_H_
#define DDK_METADATA_CLOCK_H_

typedef struct {
    uint32_t clock_count;
    uint32_t clock_ids[];
} clock_id_map_t;

typedef struct {
    uint32_t map_count;
    clock_id_map_t maps[];
} clock_id_maps_t;

#endif  // DDK_METADATA_CLOCK_H_
