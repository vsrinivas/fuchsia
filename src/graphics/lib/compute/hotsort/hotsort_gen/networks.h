// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#pragma once

//
//
//

#include "gen.h"

//
//
//

struct hsg_network
{
  uint32_t      const   length;
  struct hsg_op const * network;
};

//
//
//

// Sorting networks of size 2 to 64.
extern struct hsg_network const hsg_networks_sorting[];

// Odd-merge merging networks of size 2 to 64.
extern struct hsg_network const hsg_networks_merging[];

//
//
//
