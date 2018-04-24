// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/disassembler.h"

#include <inttypes.h>
#include <limits>

#include "garnet/bin/zxdb/client/output_buffer.h"
#include "garnet/bin/zxdb/client/session_llvm_state.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "garnet/public/lib/fxl/strings/trim.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/Support/TargetRegistry.h"

namespace zxdb {

namespace {

// In-place replaces instances of ANY of the charcters in "search_for" with the
// given replacement in the given string.
void ReplaceAllInstancesOf(const char* search_for,
                           char replace_with,
                           std::string* str) {
  size_t found_pos = 0;
  while ((found_pos = str->find_first_of(search_for, found_pos)) !=
         std::string::npos) {
    (*str)[found_pos] = replace_with;
  }
}

std::string GetInvalidInstructionStr(const uint8_t* data, size_t len) {
  std::string result("\t.byte\t");
  for (size_t i = 0; i < len; i++) {
    if (i > 0)
      result.push_back(' ');
    result.append(fxl::StringPrintf("0x%2.2x", data[i]));
  }
  return result;
}

}  // namespace

Disassembler::Disassembler() = default;
Disassembler::~Disassembler() = default;

Err Disassembler::Init(SessionLLVMState* llvm) {
  llvm_ = llvm;

  context_ = std::make_unique<llvm::MCContext>(llvm->asm_info(),
                                               llvm_->register_info(), nullptr);
  disasm_.reset(llvm_->target()->createMCDisassembler(*llvm_->subtarget_info(),
                                                      *context_));
  if (!disasm_)
    return Err("Couldn't create LLVM disassembler.");

  constexpr int kAssemblyFlavor = 1;  // 1 means "Intel" (not AT&T).
  printer_.reset(llvm_->target()->createMCInstPrinter(
      *llvm_->triple(), kAssemblyFlavor, *llvm_->asm_info(),
      *llvm_->instr_info(), *llvm->register_info()));
  printer_->setPrintHexStyle(llvm::HexStyle::C);  // ::C = 0xff-style.
  printer_->setPrintImmHex(true);
  printer_->setUseMarkup(true);

  return Err();
}

size_t Disassembler::DisassembleOne(const uint8_t* data,
                                    size_t data_len,
                                    uint64_t address,
                                    const Options& options,
                                    OutputBuffer* out) {
  llvm::MCInst inst;
  size_t consumed = 0;
  auto status = disasm_->getInstruction(inst, consumed,
                                        llvm::ArrayRef<uint8_t>(data, data_len),
                                        address, llvm::nulls(), llvm::nulls());

  std::string inst_string;
  std::string comment_string;
  if (status == llvm::MCDisassembler::Success) {
    // Print the instruction.
    llvm::raw_string_ostream inst_stream(inst_string);
    llvm::raw_string_ostream comment_stream(comment_string);

    printer_->setCommentStream(comment_stream);
    printer_->printInst(&inst, inst_stream, llvm::StringRef(),
                        *llvm_->subtarget_info());
    printer_->setCommentStream(llvm::nulls());

    inst_stream.flush();
    comment_stream.flush();
  } else {
    // Failure decoding.
    if (!options.emit_undecodable)
      return 0;

    consumed =
        std::min(data_len,
                 static_cast<size_t>(llvm_->asm_info()->getMinInstAlignment()));
    inst_string = GetInvalidInstructionStr(data, consumed);
    comment_string = "Invalid instruction.";
  }

  // Address.
  if (options.emit_addresses) {
    out->Append(Syntax::kComment, fxl::StringPrintf("\t0x%16.16" PRIx64,
                address));
  }

  // Bytes.
  if (options.emit_bytes) {
    std::string bytes("\t");
    for (size_t i = 0; i < consumed; i++) {
      if (i > 0)
        bytes.push_back(' ');
      bytes.append(fxl::StringPrintf("%2.2x", data[i]));
    }
    out->Append(Syntax::kComment, bytes);
  }

  // Instruction.
  out->Append(Syntax::kNormal, inst_string);

  // Comments.
  if (!comment_string.empty()) {
    // Canonicalize the comments, they'll end in a newline (which is added
    // manually later) and may contain embedded newlines.
    comment_string = fxl::TrimString(comment_string, "\r\n ").ToString();
    ReplaceAllInstancesOf("\r\n", ' ', &comment_string);

    out->Append(Syntax::kComment,
                "\t" + llvm_->asm_info()->getCommentString().str() + " " +
                    comment_string);
  }

  out->Append(Syntax::kNormal, "\n");
  return consumed;
}

size_t Disassembler::DisassembleMany(const uint8_t* data,
                                     size_t data_len,
                                     uint64_t start_address,
                                     const Options& in_options,
                                     size_t max_instructions,
                                     OutputBuffer* out) {
  size_t instruction_count = 0;

  if (max_instructions == 0)
    max_instructions = std::numeric_limits<size_t>::max();

  // Force emit_undecodable to true or we can never advance past undecodable
  // instructions.
  Options options = in_options;
  options.emit_undecodable = true;

  size_t byte_offset = 0;
  while (byte_offset < data_len && instruction_count < max_instructions) {
    size_t bytes_read =
        DisassembleOne(&data[byte_offset], data_len - byte_offset,
                       start_address + byte_offset, options, out);
    FXL_DCHECK(bytes_read > 0);

    instruction_count++;
    byte_offset += bytes_read;
  }
  return byte_offset;
}

}  // namespace zxdb
