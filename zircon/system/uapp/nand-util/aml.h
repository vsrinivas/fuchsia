// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fuchsia/hardware/nand/c/fidl.h>

// Prints out some data from the "first page".
void DumpPage0(const void* data);

// Returns the location and size of the bad block table. |data| must be a Page0
// buffer.
void GetBbtLocation(const void* data, uint32_t* first_block, uint32_t* num_blocks);

// Prints out the bad blocks from the bad block tables. Returns the number of
// tables parsed. |data| and |oob| must contain the data from a full erase block.
// Note that GetBbtLocation() has to be called before using this function, to
// determine what erase blocks to read.
int DumpBbt(const void* data, const void* oob, const fuchsia_hardware_nand_Info& info);
