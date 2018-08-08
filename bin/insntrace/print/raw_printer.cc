// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_printer.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "garnet/lib/intel_pt_decode/decoder.h"

#include "third_party/simple-pt/printer-util.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

namespace intel_processor_trace {

std::unique_ptr<RawPrinter> RawPrinter::Create(DecoderState* state,
                                               const Config& config) {
  std::string output_file_name = config.output_file_name;
  FILE* out_file = stdout;
  if (config.output_file_name != "") {
    out_file = fopen(output_file_name.c_str(), "w");
    if (!out_file) {
      FXL_LOG(ERROR) << "Unable to open file for writing: "
                     << config.output_file_name;
      return nullptr;
    }
  }

  auto printer =
      std::unique_ptr<RawPrinter>(new RawPrinter(out_file, state, config));
  return printer;
}

RawPrinter::RawPrinter(FILE* out_file, DecoderState* state,
                       const Config& config)
    : out_file_(out_file), state_(state), config_(config) {}

RawPrinter::~RawPrinter() {
  if (config_.output_file_name != "")
    fclose(out_file_);
}

void RawPrinter::Printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(out_file_, format, args);
  va_end(args);
}

RawPrinter::Space RawPrinter::GetSpace(uint64_t cr3,
                                       const SymbolTable* symtab) {
  if (symtab) {
    if (symtab->is_kernel())
      return Space::kKernel;
    else
      return Space::kUser;
  } else if (cr3 != pt_asid_no_cr3) {
    // If we're in kernel space on behalf of userspace that is intended to
    // be caught by the preceding case (symtab != nullptr).
    if (cr3 == state_->kernel_cr3())
      return Space::kKernel;
    else
      return Space::kUser;
  } else {
    return Space::kUnknown;
  }
}

void RawPrinter::PrintInsn(const pt_insn* insn, PrintState* ps) {
  Printf("%" PRIu64 ": %" PRIx64 ": %s", ps->current_ts, ps->current_pc,
         simple_pt::InsnClassName(insn->iclass));
  // TODO(dje): Add option to include disassembly.
  Printf("\n");
}

int RawPrinter::ProcessNextInsn(struct pt_insn_decoder* pt_decoder,
                                PrintState* ps) {
  // This is the data we obtain from libipt.
  struct pt_insn insn;

  // Do the increment before checking the result of pt_insn_next so that
  // error lines have reference numbers as well.
  ++ps->total_insncnt;

  pt_insn_get_offset(pt_decoder, &ps->current_pos);

  // TODO(dje): Verify this always stores values in the arguments even
  // if there's an error (which according to intel-pt.h can only be
  // -pte_no_time).
  uint64_t ts;
  uint32_t lost_mtc, lost_cyc;
  pt_insn_time(pt_decoder, &ts, &lost_mtc, &lost_cyc);
  if (ts)
    ps->current_ts = ts;

  insn.ip = 0;
  int err = pt_insn_next(pt_decoder, &insn, sizeof(insn));
  uint64_t cr3;
  pt_insn_get_cr3(pt_decoder, &cr3);
  ps->current_pc = insn.ip;

  if (err < 0) {
    ps->current_cr3 = cr3;
    return err;
  }

  // Watch for changes to the core bus ratio recorded in the trace.
  uint32_t ratio;
  pt_insn_core_bus_ratio(pt_decoder, &ratio);
  if (ratio != ps->current_core_bus_ratio) {
    Printf("Core bus ratio is now %u\n", ratio);
    ps->current_core_bus_ratio = ratio;
  }

  // Watch for changes to CR3.
  if (cr3 != ps->current_cr3) {
    Printf("CR3 is now 0x%" PRIx64 "\n", cr3);
    ps->current_cr3 = cr3;
  }

  const SymbolTable* symtab =
      state_->FindSymbolTable(ps->current_cr3, ps->current_pc);
  const Symbol* sym = symtab ? symtab->FindSymbol(ps->current_pc) : nullptr;

  Space space = GetSpace(ps->current_cr3, symtab);
  if (space != ps->current_space) {
    Printf("Space is now ");
    switch (space) {
      case Space::kKernel:
        Printf("kernel");
        break;
      case Space::kUser:
        Printf("user");
        break;
      default:
        Printf("unknown");
        break;
    }
    Printf("\n");
    ps->current_space = space;
  }

  // Watch for changes to the current function.
  if (sym != ps->current_function) {
    if (sym) {
      Printf("Current function is now %s:%s\n", symtab->file_name().c_str(),
             sym->name ? sym->name : "unknown");
    } else {
      Printf("Entering unknown function\n");
    }
    ps->current_symtab = symtab;
    ps->current_function = sym;
  }

  PrintInsn(&insn, ps);

  return 0;
}

uint64_t RawPrinter::PrintOneFile(const PtFile& pt_file) {
  if (!state_->AllocDecoder(pt_file.file)) {
    FXL_LOG(ERROR) << "Unable to open pt file: " << pt_file.file;
    return 0;
  }

  Printf("Dump of PT file %s, id 0x%" PRIx64 "\n", pt_file.file.c_str(),
         pt_file.id);

  PrintState ps;

  struct pt_insn_decoder* pt_decoder = state_->decoder();

  for (;;) {
    // Every time we get an error while reading the trace we start over
    // at the top of this loop.

    int err = pt_insn_sync_forward(pt_decoder);
    pt_insn_get_offset(pt_decoder, &ps.current_pos);
    if (err < 0) {
      std::string message =
          fxl::StringPrintf("0x%" PRIx64 ": sync forward: %s\n", ps.current_pos,
                            pt_errstr(pt_errcode(err)));
      if (err == -pte_eos) {
        FXL_LOG(INFO) << message;
      } else {
        FXL_LOG(ERROR) << message;
      }
      break;
    }

    for (;;) {
      err = ProcessNextInsn(pt_decoder, &ps);
      if (err < 0)
        break;
    }

    if (err == -pte_eos) {
      // Let the top of the loop test catch and report this.
      continue;
    }

    FXL_LOG(ERROR) << fxl::StringPrintf(
        "[%8" PRIu64 "] @0x%" PRIx64 ": %" PRIx64 ":%" PRIx64 ": error %s",
        ps.total_insncnt, ps.current_pos, ps.current_cr3, ps.current_pc,
        pt_errstr(pt_errcode(err)));
  }

  state_->FreeDecoder();

  return ps.total_insncnt;
}

uint64_t RawPrinter::PrintFiles() {
  uint64_t total_insns = 0;

  for (const auto& file : state_->pt_files()) {
    total_insns += PrintOneFile(file);
  }

  return total_insns;
}

}  // namespace intel_processor_trace
