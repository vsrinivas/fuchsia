// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_GPT_GPT_H_
#define SRC_STORAGE_GPT_GPT_H_

#include <gpt/gpt.h>
#include <mbr/mbr.h>

namespace gpt {

// Create a mbr::Mbr containing a single protective MBR partition,
// covering the whole disk.
//
// A protective MBR avoids legacy operating systems from incorrectly
// detecting the disk as containing no data (when in fact it is using
// a GPT) and possibly attempting to format the disk, etc.
mbr::Mbr MakeProtectiveMbr(uint64_t blocks_in_disk);

}  // namespace gpt

#endif  // SRC_STORAGE_GPT_GPT_H_
