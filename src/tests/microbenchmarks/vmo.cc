// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the time taken to write or read a chunk of data to/from a VMO
// using the zx_vmo_write() or zx_vmo_read() syscalls respectively.
bool VmoReadOrWriteTest(perftest::RepeatState* state, uint32_t copy_size, bool do_write) {
  state->SetBytesProcessedPerRun(copy_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(copy_size, 0, &vmo));
  std::vector<char> buffer(copy_size);

  // Write the buffer so that the pages are pre-committed. This matters
  // more for the read case.
  ASSERT_OK(vmo.write(buffer.data(), 0, copy_size));

  if (do_write) {
    while (state->KeepRunning()) {
      ASSERT_OK(vmo.write(buffer.data(), 0, copy_size));
    }
  } else {
    while (state->KeepRunning()) {
      ASSERT_OK(vmo.read(buffer.data(), 0, copy_size));
    }
  }
  return true;
}

// Measure the time taken to write or read a chunk of data to/from a mapped VMO. The writing/reading
// is either done from userland using memcpy() (when user_memcpy=true) or by the kernel using
// zx_vmo_read()/zx_vmo_write() (when user_memcpy=false).
bool VmoReadOrWriteMapTestImpl(perftest::RepeatState* state, uint32_t copy_size, bool do_write,
                               int flags, bool user_memcpy) {
  state->SetBytesProcessedPerRun(copy_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(copy_size, 0, &vmo));
  std::vector<char> buffer(copy_size);
  zx_vaddr_t mapped_addr;

  zx::vmo vmo_buf;
  if (!user_memcpy) {
    // Create a temporary VMO that we can use to get the kernel to read/write our mapped memory.
    ASSERT_OK(zx::vmo::create(copy_size, 0, &vmo_buf));
  }

  // Write the buffer so that the pages are pre-committed. This matters
  // more for the read case.
  ASSERT_OK(vmo.write(buffer.data(), 0, copy_size));

  if (do_write) {
    while (state->KeepRunning()) {
      ASSERT_OK(zx::vmar::root_self()->map(
          0, vmo, 0, copy_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | flags, &mapped_addr));
      if (user_memcpy) {
        std::memcpy(reinterpret_cast<void*>(mapped_addr), buffer.data(), copy_size);
      } else {
        // To write to the mapped in portion we *read* from the temporary VMO.
        ASSERT_OK(vmo_buf.read(reinterpret_cast<void*>(mapped_addr), 0, copy_size));
      }
      ASSERT_OK(zx::vmar::root_self()->unmap(mapped_addr, copy_size));
    }
  } else {  // read
    while (state->KeepRunning()) {
      ASSERT_OK(zx::vmar::root_self()->map(
          0, vmo, 0, copy_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | flags, &mapped_addr));
      if (user_memcpy) {
        std::memcpy(buffer.data(), reinterpret_cast<void*>(mapped_addr), copy_size);
      } else {
        // To read from the mapped in portion we *write* it to the temporary VMO.
        ASSERT_OK(vmo_buf.write(reinterpret_cast<void*>(mapped_addr), 0, copy_size));
      }
      ASSERT_OK(zx::vmar::root_self()->unmap(mapped_addr, copy_size));
    }
  }
  return true;
}

bool VmoReadOrWriteMapTest(perftest::RepeatState* state, uint32_t copy_size, bool do_write,
                           bool user_memcpy) {
  return VmoReadOrWriteMapTestImpl(state, copy_size, do_write, 0, user_memcpy);
}

bool VmoReadOrWriteMapRangeTest(perftest::RepeatState* state, uint32_t copy_size, bool do_write,
                                bool user_memcpy) {
  return VmoReadOrWriteMapTestImpl(state, copy_size, do_write, ZX_VM_MAP_RANGE, user_memcpy);
}

// Measure the time taken to clone a vmo and destroy it. If do_map=true, then
// this function tests the case where the original vmo is mapped; otherwise
// it tests the case where the original vmo is not mapped.
bool VmoCloneTest(perftest::RepeatState* state, uint32_t copy_size, bool do_map) {
  if (do_map) {
    state->DeclareStep("map");
  }
  state->DeclareStep("clone");
  state->DeclareStep("close");
  if (do_map) {
    state->DeclareStep("unmap");
  }

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(copy_size, 0, &vmo));
  ASSERT_OK(vmo.op_range(ZX_VMO_OP_COMMIT, 0, copy_size, nullptr, 0));

  while (state->KeepRunning()) {
    zx_vaddr_t addr = 0;
    if (do_map) {
      ASSERT_OK(zx::vmar::root_self()->map(0, vmo, 0, copy_size, ZX_VM_MAP_RANGE | ZX_VM_PERM_READ,
                                           &addr));
      state->NextStep();
    }

    zx::vmo clone;
    ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, copy_size, &clone));
    state->NextStep();

    clone.reset();

    if (do_map) {
      state->NextStep();
      ASSERT_OK(zx::vmar::root_self()->unmap(addr, copy_size));
    }
  }

  return true;
}

// Measure the time it takes to clone a vmo. Specifically, this measures:
//   - Clone a vmo.
//   - Read or write either the original vmo (do_target_clone=false) or the
//     clone (do_target_clone=true).
//     - For bidirectional clones, we don't expect varying do_target_clone to
//       significantly affect this performance.
//     - do_full_op controls whether we read or write the whole vmo or just
//       a subset of the pages, as the performance characteristics of a
//       partially populated clone and a fully populated clone can differ.
//   - Destroy the clone.
bool VmoCloneReadOrWriteTest(perftest::RepeatState* state, uint32_t copy_size, bool do_write,
                             bool do_target_clone, bool do_full_op) {
  state->DeclareStep("clone");
  state->DeclareStep(do_write ? "write" : "read");
  state->DeclareStep("close");
  state->SetBytesProcessedPerRun(copy_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(copy_size, 0, &vmo));
  ASSERT_OK(vmo.op_range(ZX_VMO_OP_COMMIT, 0, copy_size, nullptr, 0));

  std::vector<char> buffer(copy_size);

  while (state->KeepRunning()) {
    zx::vmo clone;
    ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, copy_size, &clone));
    state->NextStep();

    const zx::vmo& target = do_target_clone ? clone : vmo;
    if (do_full_op) {
      if (do_write) {
        ASSERT_OK(target.write(buffer.data(), 0, copy_size));
      } else {
        ASSERT_OK(target.read(buffer.data(), 0, copy_size));
      }
    } else {
      // There's no special meaning behind the particular value of this
      // constant. It just needs to result in a couple of writes into
      // the vmo without populating it too densely.
      static constexpr uint64_t kWriteInterval = 8 * ZX_PAGE_SIZE;
      for (uint64_t offset = 0; offset < copy_size; offset += kWriteInterval) {
        if (do_write) {
          ASSERT_OK(target.write(buffer.data(), offset, PAGE_SIZE));
        } else {
          ASSERT_OK(target.read(buffer.data(), offset, PAGE_SIZE));
        }
      }
    }

    state->NextStep();
    // The clone goes out of scope and is implicitly closed.
  }

  return true;
}

template <typename Func, typename... Args>
void RegisterVmoTest(const char* name, Func fn, Args... args) {
  for (unsigned size_in_kbytes : {128, 512, 2048}) {
    auto full_name = fbl::StringPrintf("%s/%ukbytes", name, size_in_kbytes);
    perftest::RegisterTest(full_name.c_str(), fn, size_in_kbytes * 1024, args...);
  }
}

void RegisterTests() {
  for (bool do_write : {false, true}) {
    const char* rw = do_write ? "Write" : "Read";
    auto rw_name = fbl::StringPrintf("Vmo/%s", rw);
    RegisterVmoTest(rw_name.c_str(), VmoReadOrWriteTest, do_write);
  }

  for (bool do_write : {false, true}) {
    for (bool user_memcpy : {false, true}) {
      const char* rw = do_write ? "Write" : "Read";
      const char* user_kernel = user_memcpy ? "" : "/Kernel";
      auto rw_name = fbl::StringPrintf("VmoMap/%s%s", rw, user_kernel);
      RegisterVmoTest(rw_name.c_str(), VmoReadOrWriteMapTest, do_write, user_memcpy);

      rw_name = fbl::StringPrintf("VmoMapRange/%s%s", rw, user_kernel);
      RegisterVmoTest(rw_name.c_str(), VmoReadOrWriteMapRangeTest, do_write, user_memcpy);
    }
  }

  for (bool map : {false, true}) {
    auto clone_name = fbl::StringPrintf("Vmo/Clone%s", map ? "Map" : "");
    RegisterVmoTest(clone_name.c_str(), VmoCloneTest, map);
  }

  for (bool do_write : {false, true}) {
    for (bool do_target_clone : {false, true}) {
      for (bool do_full_op : {false, true}) {
        const char* rw = do_write ? "Write" : "Read";
        const char* target = do_target_clone ? "Clone" : "Orig";
        const char* density = do_full_op ? "All" : "Some";
        auto clone_rw_name = fbl::StringPrintf("Vmo/Clone/%s%s%s", rw, target, density);
        RegisterVmoTest(clone_rw_name.c_str(), VmoCloneReadOrWriteTest, do_write, do_target_clone,
                        do_full_op);
      }
    }
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
