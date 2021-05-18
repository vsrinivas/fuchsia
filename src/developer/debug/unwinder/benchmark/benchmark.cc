// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "lib/syslog/cpp/macros.h"
#include "snapshot/minidump/process_snapshot_minidump.h"
#include "src/developer/debug/unwinder/benchmark/libunwindstack.h"
#include "src/developer/debug/unwinder/benchmark/minidump_memory.h"
#include "src/developer/debug/unwinder/registers.h"
#include "src/developer/debug/unwinder/unwind.h"

namespace benchmark {

void PrintContext(const crashpad::ProcessSnapshotMinidump& minidump) {
  printf("{{{reset}}}\n");
  int module_idx = 0;
  for (auto module : minidump.Modules()) {
    printf("{{{module:%#x:%s:elf:%s}}}\n", module_idx, module->Name().c_str(),
           MinidumpGetBuildID(*module).c_str());
    printf("{{{mmap:%#" PRIx64 ":%#" PRIx64 ":load:%#x:rwx:0x0}}}\n", module->Address(),
           module->Size(), module_idx);
    module_idx++;
  }
}

void PrintBacktrace(const std::vector<unwinder::Frame>& frames) {
  int i = 0;
  for (auto& frame : frames) {
    uint64_t pc;
    uint64_t sp;
    frame.regs.GetPC(pc);
    frame.regs.GetSP(sp);
    printf("{{{bt:%d:%#" PRIx64 ":sp %#" PRIx64 "}}}\n", i++, pc, sp);
    printf("  %s\n", frame.regs.Describe().c_str());
  }
}

unwinder::Registers ParseMinidumpContext(const crashpad::CPUContext& context) {
  using unwinder::RegisterID;
  using unwinder::Registers;

  Registers res(Registers::Arch::kX64);
  switch (context.architecture) {
    case crashpad::CPUArchitecture::kCPUArchitectureX86_64:
      res.Set(RegisterID::kX64_rax, context.x86_64->rax);
      res.Set(RegisterID::kX64_rbx, context.x86_64->rbx);
      res.Set(RegisterID::kX64_rcx, context.x86_64->rcx);
      res.Set(RegisterID::kX64_rdx, context.x86_64->rdx);
      res.Set(RegisterID::kX64_rdi, context.x86_64->rdi);
      res.Set(RegisterID::kX64_rsi, context.x86_64->rsi);
      for (int i = 6; i < static_cast<int>(RegisterID::kX64_last); i++) {
        res.Set(static_cast<RegisterID>(i), reinterpret_cast<uint64_t*>(context.x86_64)[i]);
      }
      break;
    case crashpad::CPUArchitecture::kCPUArchitectureARM64:
      res = Registers(Registers::Arch::kArm64);
      for (int i = 0; i < static_cast<int>(RegisterID::kArm64_last); i++) {
        res.Set(static_cast<RegisterID>(i), reinterpret_cast<uint64_t*>(context.arm64)[i]);
      }
      break;
    default:
      FX_NOTREACHED();
  }
  return res;
}

std::vector<unwinder::Frame> UnwindFromUnwinder(
    const std::shared_ptr<MinidumpMemory>& memory,
    const std::vector<const crashpad::ModuleSnapshot*>& modules,
    const crashpad::ThreadSnapshot* thread) {
  std::vector<uint64_t> module_bases;
  module_bases.reserve(modules.size());
  for (auto module : modules) {
    module_bases.push_back(module->Address());
  }
  auto context = thread->Context();
  return unwinder::Unwind(memory.get(), module_bases, ParseMinidumpContext(*context));
}

int Main(int argc, const char** argv) {
  if (argc < 2) {
    printf("Usage: %s <minidump.dmp>\n", argv[0]);
    printf("Please make sure that all symbols are available in the symbol-index.\n");
    return 1;
  }

  crashpad::FileReader reader;
  if (!reader.Open(base::FilePath(argv[1]))) {
    printf("cannot open %s", argv[1]);
    return 1;
  }
  crashpad::ProcessSnapshotMinidump minidump;
  FX_CHECK(minidump.Initialize(&reader));
  reader.Close();

  PrintContext(minidump);

  auto memory = std::make_shared<MinidumpMemory>(minidump);
  auto thread = minidump.Threads()[0];
  if (auto exception = minidump.Exception()) {
    for (auto th : minidump.Threads()) {
      if (th->ThreadID() == exception->ThreadID()) {
        thread = th;
      }
    }
  }

  printf("Result from libunwindstack:\n");
  memory->ResetStatistics();
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  PrintBacktrace(UnwindFromLibunwindstack(memory, minidump.Modules(), thread));
  printf("Time elapsed: %lld ns. Memory access: %" PRIu64 " times / %" PRIu64 " bytes\n",
         std::chrono::high_resolution_clock::now().time_since_epoch().count() - now,
         memory->GetStatistics().read_count, memory->GetStatistics().total_read_size);

  printf("Result from unwinder:\n");
  memory->ResetStatistics();
  now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  PrintBacktrace(UnwindFromUnwinder(memory, minidump.Modules(), thread));
  printf("Time elapsed: %lld ns. Memory access: %" PRIu64 " times / %" PRIu64 " bytes\n",
         std::chrono::high_resolution_clock::now().time_since_epoch().count() - now,
         memory->GetStatistics().read_count, memory->GetStatistics().total_read_size);

  return 0;
}

}  // namespace benchmark

int main(int argc, const char** argv) { return benchmark::Main(argc, argv); }
