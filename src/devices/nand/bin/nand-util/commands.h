// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "nand-broker.h"

// Prints out information about bad block tables.
bool FindBadBlocks(const NandBroker& nand);

// Verifies that reads always return the same data.
bool ReadCheck(const NandBroker& nand, uint32_t first_block, uint32_t count);

// Reads a single block page by page, ignoring any read errors. Data is stored
// in the NandBroker's buffer.
void ReadBlockByPage(const NandBroker& nand, uint32_t block_num);

// Saves data from a nand device to a file at |path|.
bool Save(const NandBroker& nand, uint32_t first_block, uint32_t count, const char* path);

// Erases blocks from a nand device.
bool Erase(const NandBroker& nand, uint32_t first_block, uint32_t count);
