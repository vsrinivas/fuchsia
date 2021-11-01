// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <safemath/checked_math.h>

#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/third_party/ext2_hash/hash.h"

namespace f2fs {

f2fs_hash_t DentryHash(std::string_view name) {
  uint32_t hash;
  __UNUSED uint32_t minor_hash;
  f2fs_hash_t f2fs_hash;
  uint32_t in[8], buf[4];

  int len = safemath::checked_cast<int>(name.length());

  if (len <= 2 &&
      ((len == 1 && name[0] == '.') || (len == 2 && name[1] == '.' && name[2] == '.'))) {
    return 0;
  }

  /* Initialize the default seed for the hash checksum functions */
  buf[0] = 0x67452301;
  buf[1] = 0xefcdab89;
  buf[2] = 0x98badcfe;
  buf[3] = 0x10325476;

  const char *p = name.data();
  while (len > 0) {
    Str2HashBuf(p, len, in, 4);
    TEATransform(buf, in);
    len -= 16;
    p += 16;
  }
  hash = buf[0];
  minor_hash = buf[1];

  f2fs_hash = hash;
  f2fs_hash &= ~kHashColBit;
  return f2fs_hash;
}

}  // namespace f2fs
