// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_F2FS_TYPES_H_
#define SRC_STORAGE_F2FS_F2FS_TYPES_H_

#include <sys/types.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <atomic>

#ifdef __Fuchsia__
#include <threads.h>
#endif  // __Fuchsia__

namespace f2fs {

class Page;
class VnodeF2fs;

using sector_t = uint64_t;
using block_t = uint32_t;
using f2fs_hash_t = uint32_t;
using gfp_t = uint32_t;
using nid_t = uint32_t;
using ino_t = uint32_t;
using pgoff_t = uint64_t;
using atomic_t = std::atomic_int;
using umode_t = uint16_t;
using VnodeCallback = fit::function<zx_status_t(fbl::RefPtr<VnodeF2fs> &)>;
using PageCallback = fit::function<zx_status_t(fbl::RefPtr<Page> &)>;

#if BYTE_ORDER == BIG_ENDIAN
inline uint16_t LeToCpu(uint16_t x) { return SWAP_16(x); }
inline uint32_t LeToCpu(uint32_t x) { return SWAP_32(x); }
inline uint64_t LeToCpu(uint64_t x) { return SWAP_64(x); }
inline uint16_t CpuToLe(uint16_t x) { return SWAP_16(x); }
inline uint32_t CpuToLe(uint32_t x) { return SWAP_32(x); }
inline uint64_t CpuToLe(uint64_t x) { return SWAP_64(x); }
#else
inline uint16_t LeToCpu(uint16_t x) { return x; }
inline uint32_t LeToCpu(uint32_t x) { return x; }
inline uint64_t LeToCpu(uint64_t x) { return x; }
inline uint16_t CpuToLe(uint16_t x) { return x; }
inline uint32_t CpuToLe(uint32_t x) { return x; }
inline uint64_t CpuToLe(uint64_t x) { return x; }
#endif

constexpr size_t kPageSize = 4096;
constexpr size_t kBitsPerByte = 8;
constexpr size_t kPageCacheShift = 12;
constexpr size_t kF2fsSuperMagic = 0xF2F52010;
constexpr size_t kCrcPolyLe = 0xedb88320;
constexpr size_t kAopWritepageActivate = 0x80000;

constexpr size_t kRead = 0x0;
constexpr size_t kWrite = 0x1;
constexpr size_t kFlush = 0x2;
constexpr size_t kFua = 0x4;
constexpr size_t kDiscard = 0x08;
constexpr size_t kSync = 0x10;
constexpr size_t kReadSync = (kRead | kSync);
constexpr size_t kWriteSync = (kWrite | kSync);
constexpr size_t kWriteFlushFua = (kWrite | kSync | kFlush | kFua);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_TYPES_H_
