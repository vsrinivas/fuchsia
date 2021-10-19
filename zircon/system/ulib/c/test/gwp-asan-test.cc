// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf-search.h>
#include <elf.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/syscalls/exception.h>

#include <zxtest/zxtest.h>

#include "zircon/system/ulib/c/scudo/gwp_asan_info.h"

namespace {

constexpr const char* kHelperPath = "/pkg/bin/gwp-asan-test-helper";

TEST(GwpAsanTest, HandleGwpAsanException) {
  if constexpr (!HAS_GWP_ASAN) {
    return;
  }
  // Create a job and attach an exception channel.
  zx::job test_job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &test_job));
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });
  zx::channel exception_channel;
  ASSERT_OK(test_job.create_exception_channel(0, &exception_channel));

  // Spawn the helper process.
  const char* argv[] = {kHelperPath, nullptr};
  const char* envp[] = {
      "SCUDO_OPTIONS="
      "GWP_ASAN_Enabled=true:GWP_ASAN_SampleRate=1:GWP_ASAN_MaxSimultaneousAllocations=512",
      nullptr};
  zx::process test_process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_OK(fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL, kHelperPath, argv, envp, 0,
                           nullptr, test_process.reset_and_get_address(), err_msg),
            "%s", err_msg);

  // Wait for the helper to crash or the process to terminate.
  zx_wait_item_t wait_items[] = {
      {.handle = exception_channel.get(), .waitfor = ZX_CHANNEL_READABLE, .pending = 0},
      {.handle = test_process.get(), .waitfor = ZX_PROCESS_TERMINATED, .pending = 0},
  };
  ASSERT_OK(zx_object_wait_many(wait_items, 2, ZX_TIME_INFINITE));

  // The helper should crash and the exception channel should signal.
  ASSERT_TRUE(wait_items[0].pending & ZX_CHANNEL_READABLE);
  ASSERT_FALSE(wait_items[1].pending & ZX_PROCESS_TERMINATED);

  zx_exception_info_t info;
  zx::exception exception;
  ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr));
  ASSERT_EQ(ZX_EXCP_FATAL_PAGE_FAULT, info.type);

  // The address of __libc_gwp_asan_info.
  uint64_t libc_gwp_asan_info_addr = 0;

  // Find the GWP-ASan note.
  elf_search::ForEachModule(test_process, [&](const elf_search::ModuleInfo& info) {
    if (info.name != "libc.so") {
      return;
    }
    for (const auto& phdr : info.phdrs) {
      if (phdr.p_type != PT_NOTE) {
        continue;
      }
      // Read the whole segment.
      std::vector<std::byte> notes(phdr.p_memsz);
      size_t actual;
      ASSERT_OK(
          test_process.read_memory(info.vaddr + phdr.p_vaddr, notes.data(), notes.size(), &actual));
      ASSERT_EQ(notes.size(), actual);

      uint64_t p = 0;
      while (p + sizeof(Elf64_Nhdr) <= notes.size()) {
        Elf64_Nhdr& nhdr = reinterpret_cast<Elf64_Nhdr&>(notes[p]);
        p += sizeof(Elf64_Nhdr);
        p += (nhdr.n_namesz + 3) & ~3;
        if (nhdr.n_type == GWP_ASAN_NOTE_TYPE) {
          ASSERT_EQ(sizeof(uint64_t), nhdr.n_descsz);
          ASSERT_TRUE(p + nhdr.n_descsz <= notes.size());
          libc_gwp_asan_info_addr =
              reinterpret_cast<uint64_t&>(notes[p]) + p + info.vaddr + phdr.p_vaddr;
          return;
        }
        p += (nhdr.n_descsz + 3) & ~3;
      }
    }
  });

  // Read the __libc_gwp_asan_info.
  ASSERT_NE(0, libc_gwp_asan_info_addr);
  gwp_asan::LibcGwpAsanInfo libc_gwp_asan_info;
  size_t actual;
  ASSERT_OK(test_process.read_memory(libc_gwp_asan_info_addr, &libc_gwp_asan_info,
                                     sizeof(libc_gwp_asan_info), &actual));
  ASSERT_EQ(actual, sizeof(libc_gwp_asan_info));

  // Read the allocator state and the allocator metadata.
  gwp_asan::AllocatorState state;
  ASSERT_OK(test_process.read_memory(reinterpret_cast<uintptr_t>(libc_gwp_asan_info.state), &state,
                                     sizeof(state), &actual));
  ASSERT_EQ(actual, sizeof(state));
  std::vector<gwp_asan::AllocationMetadata> metadata_list(state.MaxSimultaneousAllocations);
  ASSERT_OK(test_process.read_memory(
      reinterpret_cast<uintptr_t>(libc_gwp_asan_info.metadata), metadata_list.data(),
      sizeof(gwp_asan::AllocationMetadata) * metadata_list.size(), &actual));
  ASSERT_EQ(actual, sizeof(gwp_asan::AllocationMetadata) * metadata_list.size());

  // Magic and version should match.
  ASSERT_EQ(0, memcmp(gwp_asan::AllocatorVersionMagic::kAllocatorVersionMagic,
                      state.VersionMagic.Magic, 4));
  ASSERT_EQ(gwp_asan::AllocatorVersionMagic::kAllocatorVersion, state.VersionMagic.Version);

  // Since it's a use-after-free, ErrorPtr must be provided for the correct detection.
  ASSERT_FALSE(__gwp_asan_error_is_mine(&state, 0));
  // And it's not an internal error.
  ASSERT_EQ(0, __gwp_asan_get_internal_crash_address(&state));

  // Read the faulting address.
  zx::thread thread;
  ASSERT_OK(exception.get_thread(&thread));
  zx_exception_report_t exception_report;
  ASSERT_OK(thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &exception_report,
                            sizeof(exception_report), nullptr, nullptr));
#if defined(__x86_64__)
  uint64_t faulting_addr = exception_report.context.arch.u.x86_64.cr2;
#elif defined(__aarch64__)
  uint64_t faulting_addr = exception_report.context.arch.u.arm_64.far;
#endif

  // Now we should be able to obtain the full report of the crash.
  ASSERT_TRUE(__gwp_asan_error_is_mine(&state, faulting_addr));
  ASSERT_EQ(gwp_asan::Error::USE_AFTER_FREE,
            __gwp_asan_diagnose_error(&state, metadata_list.data(), faulting_addr));
  const gwp_asan::AllocationMetadata* metadata =
      __gwp_asan_get_metadata(&state, metadata_list.data(), faulting_addr);
  ASSERT_NE(nullptr, metadata);
  ASSERT_TRUE(__gwp_asan_is_deallocated(metadata));

  // Allocation and free backtraces should contain at least 3 frames.
  uintptr_t backtrace[16];
  ASSERT_GE(__gwp_asan_get_allocation_trace(metadata, backtrace, 16), 3);
  ASSERT_GE(__gwp_asan_get_deallocation_trace(metadata, backtrace, 16), 3);
}

}  // namespace
