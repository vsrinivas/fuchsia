/*
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "call-printer.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "garnet/lib/intel_pt_decode/decoder.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

#include "printer-util.h"

#ifdef HAVE_UDIS86  // TODO(dje): Add disassembly support
#include <udis86.h>
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define container_of(ptr, type, member) \
  ((type*)((char*)(ptr)-offsetof(type, member)))

namespace simple_pt {

// The number of instructions we process at a time.
constexpr uint32_t kInsnsPerIteration = 256u;

std::unique_ptr<CallPrinter> CallPrinter::Create(DecoderState* state,
                                                 const Config& config) {
  FILE* f = stdout;
  if (config.output_file_name != "") {
    f = fopen(config.output_file_name.c_str(), "w");
    if (!f) {
      FXL_LOG(ERROR) << "Unable to open file for writing: "
                     << config.output_file_name;
      return nullptr;
    }
  }

  auto printer =
      std::unique_ptr<CallPrinter>(new CallPrinter(f, state, config));
  return printer;
}

CallPrinter::CallPrinter(FILE* output, DecoderState* state,
                         const Config& config)
    : out_file_(output), state_(state), config_(config) {}

CallPrinter::~CallPrinter() {
  if (config_.output_file_name != "")
    fclose(out_file_);
}

void CallPrinter::Printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(out_file_, format, args);
  va_end(args);
}

void CallPrinter::PrintPc(const SymbolTable* symtab, const Symbol* sym,
                          uint64_t pc, uint64_t cr3, bool print_cr3) {
  if (sym) {
    Printf("%s", sym->name);
    uint64_t sym_addr = sym->addr + symtab->offset();
    if (pc - sym_addr > 0)
      Printf("+%ld", pc - sym_addr);
    if (config_.dump_pc) {
      Printf(" [");
      if (print_cr3)
        Printf("%lx:", cr3);
      Printf("%lx]", pc);
    }
  } else {
    if (print_cr3)
      Printf("%lx:", cr3);
    Printf("%lx", pc);
  }
}

void CallPrinter::PrintPc(uint64_t pc, uint64_t cr3, bool print_cr3) {
  const SymbolTable* symtab;
  const Symbol* sym = state_->FindSymbol(cr3, pc, &symtab);
  PrintPc(symtab, sym, pc, cr3, print_cr3);
}

void CallPrinter::PrintEv(const char* name, const Instruction* insn) {
  Printf("%s ", name);
  PrintPc(insn->pc, insn->cr3, true);
  Printf("\n");
}

void CallPrinter::PrintEvent(const Instruction* insn) {
#if 0  // TODO(dje) Until these flags are reliable in libipt...
  if (insn->enabled)
    PrintEv("enabled", insn);
  if (insn->disabled)
    PrintEv("disabled", insn);
  if (insn->resumed)
    PrintEv("resumed", insn);
#endif
  if (insn->interrupted)
    PrintEv("interrupted", insn);
  if (insn->resynced)
    PrintEv("resynced", insn);
  if (insn->stopped)
    PrintEv("stopped", insn);
}

static bool HasSpeculativeEvent(const Instruction* insn) {
  return insn->speculative || insn->aborted || insn->committed;
}

void CallPrinter::PrintSpeculativeEvent(const Instruction* insn,
                                        int* prev_spec, int* indent) {
  if (insn->speculative != *prev_spec) {
    *prev_spec = insn->speculative;
    Printf("%*stransaction\n", *indent, "");
    *indent += 4;
  }
  if (insn->aborted) {
    Printf("%*saborted\n", *indent, "");
    *indent -= 4;
  }
  if (insn->committed) {
    Printf("%*scommitted\n", *indent, "");
    *indent -= 4;
  }
  if (*indent < 0)
    *indent = 0;
}

void CallPrinter::PrintTic(uint64_t tic) {
  Printf("[%8" PRIu64 "] ", tic);
}

void CallPrinter::PrintTimeIndent() {
  Printf("%*s", 24, "");
}

void CallPrinter::PrintTime(uint64_t ts,
                            uint64_t* first_ts, uint64_t* last_ts) {
  if (!*first_ts && !config_.abstime)
    *first_ts = ts;
  if (!*last_ts)
    *last_ts = ts;
  uint64_t relative_time = ts - *first_ts;
  uint64_t delta_time = ts - *last_ts;
  Printf("%-24s",
         fxl::StringPrintf("%-9" PRIu64 " [%-" PRIu64 "]",
                           relative_time, delta_time).c_str());
  *last_ts = ts;
}

#ifdef HAVE_UDIS86

struct dis {
  ud_t ud_obj;
  DecoderState* state;
  uint64_t cr3;
};

static const char* DisResolve(struct ud* u, uint64_t addr, int64_t* off) {
  struct dis* d = container_of(u, struct dis, ud_obj);
  Sym* sym = d->state->FindSym(d->cr3, addr);
  if (sym) {
    *off = addr - sym->val;
    return sym->name;
  } else {
    return nullptr;
  }
}

static void InitDis(struct dis* d) {
  ud_init(&d->ud_obj);
  ud_set_syntax(&d->ud_obj, UD_SYN_ATT);
  ud_set_sym_resolver(&d->ud_obj, DisResolve);
}

#else

struct dis {};
static void InitDis(struct dis* d) {}

#endif

#define NUM_WIDTH 35

void CallPrinter::PrintInsn(const struct pt_insn* insn,
                            uint64_t total_insn_count,
                            uint64_t ts, struct dis* d, uint64_t cr3) {
  int i;
  int n;
  Printf("[%8" PRIu64 "] %" PRIx64 " %" PRIu64 " %7s: ",
         total_insn_count, insn->ip, ts, InsnClassName(insn->iclass));
  n = 0;
  for (i = 0; i < insn->size; i++) {
    Printf("%02x ", insn->raw[i]);
    n += 3;
  }
#ifdef HAVE_UDIS86
  d->state = state;
  d->cr3 = cr3;
  if (insn->mode == ptem_32bit)
    ud_set_mode(&d->ud_obj, 32);
  else
    ud_set_mode(&d->ud_obj, 64);
  ud_set_pc(&d->ud_obj, insn->pc);
  ud_set_input_buffer(&d->ud_obj, insn->raw, insn->size);
  ud_disassemble(&d->ud_obj);
  Printf("%*s%s", NUM_WIDTH - n, "", ud_insn_asm(&d->ud_obj));
#endif
  if (insn->enabled)
    Printf("\tENA");
  if (insn->disabled)
    Printf("\tDIS");
  if (insn->resumed)
    Printf("\tRES");
  if (insn->interrupted)
    Printf("\tINT");
  Printf("\n");
#if 0  // TODO(dje): use libbacktrace?
  if (dump_dwarf)
    print_addr(state->FindIpFn(insn->pc, cr3), insn->pc);
#endif
}

void CallPrinter::PrintKernelMarker(const Instruction* insn,
                                    const SymbolTable* symtab) {
  if (symtab) {
    if (symtab->is_kernel())
      Printf(" K");
    else
      Printf(" U");
  } else if (insn->cr3 != pt_asid_no_cr3) {
    // If we're in kernel space on behalf of userspace then that is intended to
    // be caught by the preceding case (symtab != nullptr).
    if (insn->cr3 == state_->kernel_cr3())
      Printf(" K");
    else
      Printf(" U");
  } else {
    Printf(" ?");
  }
}

void CallPrinter::PrintInsnTime(const Instruction* insn,
                                GlobalPrintState* gps) {
  if (insn->ts)
    PrintTime(insn->ts, &gps->first_ts, &gps->last_ts);
  else
    PrintTimeIndent();
}

void CallPrinter::ReportLost(const Instruction* insn) {
  if (config_.report_lost_mtc_cyc && insn->ts) {
    if (insn->lost_mtc)
      Printf("  [lost-mtc:%u]", insn->lost_mtc);
    if (insn->lost_cyc)
      Printf("  [lost-cyc:%u]", insn->lost_cyc);
  }
}

void CallPrinter::PrintOutput(const Instruction* insnbuf, uint32_t count,
                              GlobalPrintState* gps, LocalPrintState* lps) {
  for (uint32_t i = 0; i < count; i++) {
    const Instruction* insn = &insnbuf[i];

    if (insn->core_bus_ratio && insn->core_bus_ratio != gps->core_bus_ratio) {
      Printf("frequency %d\n", insn->core_bus_ratio);
      gps->core_bus_ratio = insn->core_bus_ratio;
    }
    if (HasSpeculativeEvent(insn))
      PrintSpeculativeEvent(insn, &lps->prev_speculative, &lps->indent);
    if (insn->enabled || insn->disabled || insn->resumed || insn->interrupted ||
        insn->resynced || insn->stopped)
      PrintEvent(insn);

    // Note: For accurate output, the collection of instructions we do
    // here needs to match the records printed by decode.
    switch (insn->iclass) {
      case ptic_call:
      case ptic_far_call: {
        PrintTic(insn->tic);
        PrintInsnTime(insn, gps);
        Printf("[+%4u]", insn->insn_delta);
        const SymbolTable* symtab = state_->FindSymbolTable(insn->cr3, insn->pc);
        const Symbol* sym = symtab ? symtab->FindSymbol(insn->pc) : nullptr;
        PrintKernelMarker(insn, symtab);
        Printf(" %-7s", InsnClassName(insn->iclass));
        Printf(" %*s", lps->indent, "");
        PrintPc(symtab, sym, insn->pc, insn->cr3, true);
        Printf(" -> ");
        PrintPc(insn->dst, insn->cr3, false);
        ReportLost(insn);
        Printf("\n");
        lps->indent += 4;
        break;
      }
      case ptic_return:
      case ptic_far_return: {
        PrintTic(insn->tic);
        PrintInsnTime(insn, gps);
        Printf("[+%4u]", insn->insn_delta);
        const SymbolTable* symtab = state_->FindSymbolTable(insn->cr3, insn->pc);
        const Symbol* sym = symtab ? symtab->FindSymbol(insn->pc) : nullptr;
        PrintKernelMarker(insn, symtab);
        Printf(" %-7s", InsnClassName(insn->iclass));
        Printf(" %*s", lps->indent, "");
        PrintPc(symtab, sym, insn->pc, insn->cr3, true);
        ReportLost(insn);
        Printf("\n");
        lps->indent -= 4;
        if (lps->indent < 0)
          lps->indent = 0;
        break;
      }
      default: {
        // Always print if we have a time (for now).
        // Also print error records so that insn counts in the output are
        // easier to follow (more accurate).
        if (insn->ts || insn->iclass == ptic_error) {
          PrintTic(insn->tic);
          PrintInsnTime(insn, gps);
          Printf("[+%4u]", insn->insn_delta);
          const SymbolTable* symtab = state_->FindSymbolTable(insn->cr3, insn->pc);
          const Symbol* sym = symtab ? symtab->FindSymbol(insn->pc) : nullptr;
          PrintKernelMarker(insn, symtab);
          Printf(" %-7s", InsnClassName(insn->iclass));
          Printf(" %*s", lps->indent, "");
          PrintPc(symtab, sym, insn->pc, insn->cr3, true);
          ReportLost(insn);
          Printf("\n");
        }
        break;
      }
    }  // switch
  }
}

void CallPrinter::PrintHeader(uint64_t id) {
  Printf("PT dump for id %" PRIu64 "\n", id);
  Printf("%-10s %-9s %-13s %-7s %c %-7s %s\n", "REF#", "TIME", "DELTA", "INSNs",
         '@', "ICLASS", "LOCATION");
}

uint64_t CallPrinter::PrintOneFile(const PtFile& pt_file) {
  if (!state_->AllocDecoder(pt_file.file)) {
    FXL_LOG(ERROR) << "Unable to open pt file: " << pt_file.file;
    return 0;
  }
  auto free_decoder = fxl::MakeAutoCall([&]() {
      state_->FreeDecoder();
    });

  struct pt_insn_decoder* pt_decoder = state_->decoder();
  GlobalPrintState gps = {};
  // While scanning instructions, keep track of updates from the PT decoder.
  // A value of zero means "unknown".
  uint64_t last_ts = 0;
  struct dis dis;

  PrintHeader(pt_file.id);

  // These values are used to guide the printed output.
  gps.first_ts = 0;
  gps.last_ts = 0;

  // This doesn't need to be accurate, it's main purpose is to generate
  // referenceable numbers in the output. It's also used as a measure of
  // the number of instructions we've processed.
  uint64_t total_insn_count = 0;

  InitDis(&dis);

  for (;;) {
    // Every time we get an error while reading the trace we start over
    // at the top of this loop.

    LocalPrintState lps = {};

    int err = pt_insn_sync_forward(pt_decoder);
    if (err < 0) {
      uint64_t pos;
      pt_insn_get_offset(pt_decoder, &pos);
      Printf("%" PRIx64 ": sync forward: %s\n",
             pos, pt_errstr(pt_errcode(err)));
      break;
    }

    // For error reporting.
    uint64_t errcr3 = 0;
    uint64_t errip = 0;

    // Reset core bus ratio calculations.
    uint32_t prev_ratio = 0;

    // A count of the number of instructions since the last emitted record.
    unsigned int insns_since_last_emission = 0;

    do {
      // Instructions processed in this iteration.
      Instruction insnbuf[kInsnsPerIteration];
      // Index into |insnbuf|.
      uint32_t count = 0;

      // For calls we peek ahead to the next insn to see what function
      // was called. If true |insn| is already filled in.
      bool peeked_ahead = false;
      struct pt_insn raw_insn;

      while (!err && count < kInsnsPerIteration) {
        Instruction* insn = &insnbuf[count];

        // Do the increment before checking the result of pt_insn_next so that
        // error lines have reference numbers as well.
        ++total_insn_count;

        // TODO(dje): Verify this always stores values in the arguments even
        // if there's an error (which according to intel-pt.h can only be
        // -pte_no_time).
        pt_insn_time(pt_decoder, &insn->ts, &insn->lost_mtc, &insn->lost_cyc);
        if (insn->ts && insn->ts == last_ts) {
          // No change in the timestamp value, mark as unknown.
          insn->ts = 0;
        }

        if (!peeked_ahead) {
          raw_insn.ip = 0;
          err = pt_insn_next(pt_decoder, &raw_insn, sizeof(raw_insn));
          if (err < 0) {
            pt_insn_get_cr3(pt_decoder, &errcr3);
            errip = raw_insn.ip;
            if (insns_since_last_emission > 0) {
              // Emit a record for the first error in a sequence of them,
              // in part so we don't lose track of the instructions counted
              // so far.
              insn->iclass = ptic_error;
              insn->ts = 0;
              insn->tic = total_insn_count;
              insn->cr3 = errcr3;
              insn->pc = errip;
              insn->insn_delta = insns_since_last_emission;
              insns_since_last_emission = 0;
              ++count;
            }
            break;
          }
        }
        peeked_ahead = false;
        ++insns_since_last_emission;

        pt_insn_get_cr3(pt_decoder, &insn->cr3);
        if (config_.dump_insn)
          PrintInsn(&raw_insn, total_insn_count, insn->ts, &dis, insn->cr3);

        // Watch for changes to the core bus ratio recorded in the trace.
        uint32_t ratio;
        insn->core_bus_ratio = 0;
        pt_insn_core_bus_ratio(pt_decoder, &ratio);
        if (ratio != prev_ratio) {
          insn->core_bus_ratio = ratio;
          prev_ratio = ratio;
        }

        insn->iclass = raw_insn.iclass;

        // Note: For accurate output, the collection of instructions we do
        // here needs to match the records printed by PrintOutput.
        if (raw_insn.iclass == ptic_call || raw_insn.iclass == ptic_far_call) {
          insn->tic = total_insn_count;
          insn->pc = raw_insn.ip;
          insn->insn_delta = insns_since_last_emission;
          insns_since_last_emission = 0;
          ++count;
          TransferEvents(insn, &raw_insn);
          // Peek at the next insn to see what subroutine we called.
          raw_insn.ip = 0;
          err = pt_insn_next(pt_decoder, &raw_insn, sizeof(raw_insn));
          if (err < 0) {
            insn->dst = 0;
            pt_insn_get_cr3(pt_decoder, &errcr3);
            errip = raw_insn.ip;
            break;
          }
          peeked_ahead = true;
          insn->dst = raw_insn.ip;
        } else if (raw_insn.iclass == ptic_return ||
                   raw_insn.iclass == ptic_far_return ||
                   // Always print if we have a time (for now).
                   insn->ts) {
          insn->tic = total_insn_count;
          insn->pc = raw_insn.ip;
          insn->insn_delta = insns_since_last_emission;
          insns_since_last_emission = 0;
          ++count;
          TransferEvents(insn, &raw_insn);
        } else if (raw_insn.enabled || raw_insn.disabled || raw_insn.resumed ||
                   raw_insn.interrupted || raw_insn.resynced
                   || raw_insn.stopped || raw_insn.aborted) {
#if 0  // TODO(dje): experiment to get accurate instruction counts in output
          insn->tic = total_insn_count;
          insn->pc = raw_insn.ip;
          insn->insn_delta = insns_since_last_emission;
          insns_since_last_emission = 0;
          ++count;
          TransferEvents(insn, &raw_insn);
#else
          continue;
#endif
        } else {
          // not interesting
          continue;
        }

        if (insn->ts)
          last_ts = insn->ts;
      }  // while (!err && count < kInsnsPerIteration)

      PrintOutput(insnbuf, count, &gps, &lps);
    } while (err == 0);

    if (err == -pte_eos)
      break;

    {
      uint64_t pos;
      pt_insn_get_offset(pt_decoder, &pos);
      Printf("[%8" PRIu64 "] %" PRIx64 ":%" PRIx64 ":%" PRIx64 ": error %s\n",
             total_insn_count, pos, errcr3, errip, pt_errstr(pt_errcode(err)));
    }
  }

  return total_insn_count;
}

uint64_t CallPrinter::PrintFiles() {
  uint64_t total_insn_count = 0;

  for (const auto& file : state_->pt_files()) {
    total_insn_count += PrintOneFile(file);
  }

  return total_insn_count;
}

}  // namespace simple_pt
