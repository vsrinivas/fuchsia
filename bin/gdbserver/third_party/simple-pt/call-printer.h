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

#pragma once

#include <cstdio>

#include <memory>
#include <string>

#include "garnet/bin/gdbserver/lib/intel-pt-decode/decoder.h"

#include "lib/fxl/macros.h"

#include "instruction.h"

namespace simple_pt {

using DecoderState = intel_processor_trace::DecoderState;
using PtFile = intel_processor_trace::PtFile;

class CallPrinter {
 public:
  struct Config {
    // If "" then output goes to stdout.
    std::string output_file_name;

    bool abstime = false;
    bool report_lost_mtc_cyc = false;
    bool dump_pc = false;
    bool dump_insn = false;
  };

  static std::unique_ptr<CallPrinter> Create(DecoderState* decoder,
                                             const Config& config);

  ~CallPrinter();

  // Pretty-print the trace(s), printing function call/returns.
  // Returns the number of instructions processed.
  // This number is approximate in that errors for individual instructions
  // still count towards to the total.
  uint64_t PrintFiles();

  void Printf(const char* format, ...);

 private:
  struct GlobalPrintState {
    uint64_t first_ts;
    uint64_t last_ts;
    uint32_t core_bus_ratio;
  };

  struct LocalPrintState {
    int indent;
    int prev_speculative;
  };

  CallPrinter(FILE* output, DecoderState* decoder, const Config& config);

  uint64_t PrintOneFile(const PtFile& pt_file);

  void PrintHeader(uint64_t id);

  void PrintPc(const SymbolTable* symtab, const Symbol* sym,
               uint64_t pc, uint64_t cr3, bool print_cr3);
  void PrintPc(uint64_t pc, uint64_t cr3, bool print_cr3);
  void PrintEv(const char* name, const Instruction* insn);
  void PrintEvent(const Instruction* insn);
  void PrintSpeculativeEvent(const Instruction* insn,
                             int* prev_spec, int* indent);
  void PrintTic(uint64_t tic);
  void PrintTimeIndent();
  void PrintTime(uint64_t ts, uint64_t* first_ts, uint64_t* last_ts);
  void PrintInsn(const struct pt_insn* insn, uint64_t total_insncnt,
                 uint64_t ts, struct dis* d, uint64_t cr3);
  void PrintKernelMarker(const Instruction* insn, const SymbolTable* symtab);
  void PrintInsnTime(const Instruction* insn, GlobalPrintState* gps);
  void ReportLost(const Instruction* insn);
  void PrintOutput(const Instruction* insnbuf, uint32_t count,
                   GlobalPrintState* gps, LocalPrintState* lps);

  FILE* out_file_;
  DecoderState* state_;
  Config config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CallPrinter);
};

}  // namespace simple_pt
