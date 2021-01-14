// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_NAND_NAND_H_
#define SRC_DEVICES_LIB_NAND_NAND_H_

#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/hardware/nandinfo/c/banjo.h>

namespace nand {

void nand_banjo_from_fidl(const fuchsia_hardware_nand_Info source, nand_info_t* destination) {
  destination->page_size = source.page_size;
  destination->pages_per_block = source.pages_per_block;
  destination->num_blocks = source.num_blocks;
  destination->ecc_bits = source.ecc_bits;
  destination->oob_size = source.oob_size;
  destination->nand_class = source.nand_class;
  memcpy(&destination->partition_guid, &source.partition_guid, NAND_GUID_LEN);
}

void nand_fidl_from_banjo(const nand_info_t source, fuchsia_hardware_nand_Info* destination) {
  destination->page_size = source.page_size;
  destination->pages_per_block = source.pages_per_block;
  destination->num_blocks = source.num_blocks;
  destination->ecc_bits = source.ecc_bits;
  destination->oob_size = source.oob_size;
  destination->nand_class = source.nand_class;
  memcpy(&destination->partition_guid, &source.partition_guid, NAND_GUID_LEN);
}

}  // namespace nand

#endif  // SRC_DEVICES_LIB_NAND_NAND_H_
