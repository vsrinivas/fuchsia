// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_F2FS_TYPES_H_
#define SRC_STORAGE_F2FS_F2FS_TYPES_H_

#include <sys/types.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

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
using PageCallback = fit::function<zx_status_t(fbl::RefPtr<Page>)>;
using NodePageCallback = fit::function<zx_status_t(fbl::RefPtr<Page>, bool is_last_dnode)>;
using SyncCallback = fs::Vnode::SyncCallback;

// A async_dispatcher_t* is needed for some functions on Fuchsia only. In order to avoid ifdefs on
// every call that is compiled for host Fuchsia and Host, we define this as a nullptr_t type when
// compiling on host where callers should pass null and it's ignored.
//
// Prefer async_dispatcher_t* for Fuchsia-specific functions since it makes the intent more clear.
#ifdef __Fuchsia__
using FuchsiaDispatcher = async_dispatcher_t *;
#else
using FuchsiaDispatcher = std::nullptr_t;
#endif  // __Fuchsia__

// A reference to the vfs is needed for some functions. The specific vfs type is different between
// Fuchsia and Host, so define a PlatformVfs which represents the one for our current platform to
// avoid ifdefs on every call.
//
// Prefer using the appropriate vfs when the function is only used for one or the other.
#ifdef __Fuchsia__
using PlatformVfs = fs::PagedVfs;
#else
using PlatformVfs = fs::Vfs;
#endif  // __Fuchsia__

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

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kBitsPerByte = 8;
constexpr uint32_t kPageCacheShift = 12;
constexpr uint32_t kF2fsSuperMagic = 0xF2F52010;
constexpr uint32_t kCrcPolyLe = 0xedb88320;
constexpr auto kWriteTimeOut = std::chrono::seconds(60);

constexpr uint32_t kRead = 0x0;
constexpr uint32_t kWrite = 0x1;
constexpr uint32_t kFlush = 0x2;
constexpr uint32_t kFua = 0x4;
constexpr uint32_t kSync = 0x10;
constexpr uint32_t kReadSync = (kRead | kSync);
constexpr uint32_t kWriteSync = (kWrite | kSync);
constexpr uint32_t kWriteFlushFua = (kWrite | kSync | kFlush | kFua);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_TYPES_H_
