// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux-static-pie.h"

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/relro.h>
#include <lib/elfldltl/static-pie.h>
#include <link.h>
#include <sys/mman.h>

#include <initializer_list>
#include <string_view>
#include <tuple>

#include "lss.h"

namespace {

using namespace std::literals;

void WriteStderr(std::initializer_list<std::string_view> strings) {
  for (auto str : strings) {
    sys_write(2, str.data(), str.size());
  }
}

constexpr auto Panic = [](std::string_view error, auto&&... args) -> bool {
  WriteStderr({"Failure in static PIE initialization: "sv, error, "\n"sv});
  while (true) {
    sys_exit_group(127);
  }
};

size_t GetPageSize(const uintptr_t* start_sp) {
  // We need to locate and decode the auxv just to get the page size!
  using Auxv = ElfW(auxv_t);
  const Auxv* auxv;
  {
    const auto argc = start_sp[0];
    const auto* const argv = &start_sp[1];
    const auto* const envp = &argv[argc + 1];
    auto envp_end = envp;
    while (*envp_end != 0) {
      ++envp_end;
    }
    auxv = reinterpret_cast<const Auxv*>(envp_end + 1);
  }

  size_t pagesize = 0;
  for (const auto* av = auxv; av->a_type != AT_NULL; ++av) {
    if (av->a_type == AT_PAGESZ) {
      pagesize = av->a_un.a_val;
      break;
    }
  }
  if (pagesize == 0) {
    Panic("no AT_PAGESZ found in auxv!"sv);
  }
  return pagesize;
}

void ProtectRelro(uintptr_t start, size_t size) {
  if (size > 0) {
    start += elfldltl::Self<>::LoadBias();
    auto ptr = reinterpret_cast<const void*>(start);
    if (sys_mprotect(ptr, size, PROT_READ) != 0) {
      Panic("cannot mprotect RELRO"sv);
    }
  }
}

}  // namespace

// This is passed the starting value of the stack pointer as set by the kernel
// on execve.
extern "C" void StaticPieSetup(uintptr_t* start_sp) {
  elfldltl::Diagnostics diag(Panic, elfldltl::DiagnosticsPanicFlags());

  // Apply relocations.
  elfldltl::LinkStaticPie(elfldltl::Self<>(), diag);

  // Now protect the RELRO segment.
  std::apply(ProtectRelro, elfldltl::RelroBounds(elfldltl::Self<>::Phdrs(), GetPageSize(start_sp)));
}
