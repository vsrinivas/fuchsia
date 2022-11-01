// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backends.h"

namespace gigaboot {

static const PartitionMap::PartitionEntry kNucPartitions[] = {
    {GPT_DURABLE_BOOT_NAME, 0x100000, GPT_DURABLE_BOOT_TYPE_GUID},
    {GPT_FACTORY_BOOT_NAME, 0x100000, GPT_FACTORY_TYPE_GUID},
    {GUID_EFI_NAME, 0x400000, GUID_EFI_VALUE},
    {GPT_VBMETA_A_NAME, 0x100000, GPT_VBMETA_ABR_TYPE_GUID},
    {GPT_VBMETA_B_NAME, 0x100000, GPT_VBMETA_ABR_TYPE_GUID},
    {GPT_VBMETA_R_NAME, 0x100000, GPT_VBMETA_ABR_TYPE_GUID},
    {GPT_ZIRCON_A_NAME, 0x4000000, GPT_ZIRCON_ABR_TYPE_GUID},
    {GPT_ZIRCON_B_NAME, 0x4000000, GPT_ZIRCON_ABR_TYPE_GUID},
    {GPT_ZIRCON_R_NAME, 0x8000000, GPT_ZIRCON_ABR_TYPE_GUID},
    {GPT_FACTORY_NAME, 0x2000000, GPT_FACTORY_TYPE_GUID},
    {GPT_DURABLE_NAME, 0x100000, GPT_DURABLE_TYPE_GUID},
    // When actually writing partitions, fvm will take all remaining space.
    {GPT_FVM_NAME, SIZE_MAX, GPT_FVM_TYPE_GUID},

};

const cpp20::span<const PartitionMap::PartitionEntry> GetPartitionCustomizations() {
  return kNucPartitions;
}

}  // namespace gigaboot
