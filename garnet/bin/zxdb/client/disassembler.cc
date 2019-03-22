// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/disassembler.h"

#include <inttypes.h>
#include <limits>

#include "garnet/bin/zxdb/client/arch_info.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "garnet/public/lib/fxl/strings/trim.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/Support/TargetRegistry.h"
#include "src/developer/debug/ipc/records.h"

namespace zxdb {

namespace {

// In-place replaces instances of ANY of the characters in "search_for" with the
// given replacement in the given string.
void ReplaceAllInstancesOf(const char* search_for, char replace_with,
                           std::string* str) {
  size_t found_pos = 0;
  while ((found_pos = str->find_first_of(search_for, found_pos)) !=
         std::string::npos) {
    (*str)[found_pos] = replace_with;
  }
}

void GetInvalidInstructionStrs(const uint8_t* data, size_t len,
                               std::string* instruction, std::string* params,
                               std::string* comment) {
  *instruction = ".byte";
  params->clear();
  for (size_t i = 0; i < len; i++) {
    if (i > 0)
      params->push_back(' ');
    params->append(fxl::StringPrintf("0x%2.2x", data[i]));
  }
  *comment = "Invalid instruction.";
}

// LLVM generates a instructions like "\tmov\ta,b". Given a string like this
// with two tabs in the instruction input, separates the parameters ("a,b")
// off into the given params string, and strips tabs leaving only the
// instruction ("mov") in the input string.
void SplitInstruction(std::string* instruction, std::string* params) {
  params->clear();

  size_t first_char = instruction->find_first_not_of("\t");
  if (first_char == std::string::npos)
    return;  // Leave instruction unchanged if there are no tabs.

  // Trim leading tabs.
  instruction->erase(instruction->begin(), instruction->begin() + first_char);

  // Extract the params.
  size_t param_separator = instruction->find('\t');
  if (param_separator == std::string::npos)
    return;  // Leave params empty.

  // Trim off the params.
  *params = instruction->substr(param_separator + 1);
  instruction->erase(instruction->begin() + param_separator,
                     instruction->end());
}

}  // namespace

Disassembler::Row::Row() = default;

Disassembler::Row::Row(uint64_t address, const uint8_t* bytes, size_t bytes_len,
                       std::string op, std::string params, std::string comment)
    : address(address),
      bytes(bytes, bytes + bytes_len),
      op(op),
      params(params),
      comment(comment) {}
Disassembler::Row::~Row() = default;

bool Disassembler::Row::operator==(const Row& other) const {
  return address == other.address && bytes == other.bytes && op == other.op &&
         params == other.params && comment == other.comment;
}

Disassembler::Disassembler() = default;
Disassembler::~Disassembler() = default;

Err Disassembler::Init(const ArchInfo* arch) {
  arch_ = arch;

  context_ = std::make_unique<llvm::MCContext>(arch_->asm_info(),
                                               arch_->register_info(), nullptr);
  disasm_.reset(arch_->target()->createMCDisassembler(*arch_->subtarget_info(),
                                                      *context_));
  if (!disasm_)
    return Err("Couldn't create LLVM disassembler.");

  constexpr int kAssemblyFlavor = 1;  // 1 means "Intel" (not AT&T).
  printer_.reset(arch_->target()->createMCInstPrinter(
      *arch_->triple(), kAssemblyFlavor, *arch_->asm_info(),
      *arch_->instr_info(), *arch_->register_info()));
  printer_->setPrintHexStyle(llvm::HexStyle::C);  // ::C = 0xff-style.
  printer_->setPrintImmHex(true);
  printer_->setUseMarkup(true);

  return Err();
}

size_t Disassembler::DisassembleOne(const uint8_t* data, size_t data_len,
                                    uint64_t address, const Options& options,
                                    Row* out) const {
  out->address = address;

  // Decode.
  llvm::MCInst inst;
  uint64_t consumed = 0;
  auto status = disasm_->getInstruction(inst, consumed,
                                        llvm::ArrayRef<uint8_t>(data, data_len),
                                        address, llvm::nulls(), llvm::nulls());
  if (status == llvm::MCDisassembler::Success) {
    // Print the instruction. Note that LLVM appends to the strings so we need
    // to make sure they're empty before using.
    out->op.clear();
    out->comment.clear();
    llvm::raw_string_ostream inst_stream(out->op);
    llvm::raw_string_ostream comment_stream(out->comment);

    printer_->setCommentStream(comment_stream);
    printer_->printInst(&inst, inst_stream, llvm::StringRef(),
                        *arch_->subtarget_info());
    printer_->setCommentStream(llvm::nulls());

    inst_stream.flush();
    comment_stream.flush();

    SplitInstruction(&out->op, &out->params);
  } else {
    // Failure decoding.
    if (!options.emit_undecodable)
      return 0;
    consumed = std::min(data_len, arch_->instr_align());
    GetInvalidInstructionStrs(data, consumed, &out->op, &out->params,
                              &out->comment);
  }

  // Comments.
  if (!out->comment.empty()) {
    // Canonicalize the comments, they'll end in a newline (which is added
    // manually later) and may contain embedded newlines.
    out->comment = fxl::TrimString(out->comment, "\r\n ").ToString();
    ReplaceAllInstancesOf("\r\n", ' ', &out->comment);

    out->comment =
        arch_->asm_info()->getCommentString().str() + " " + out->comment;
  }

  out->bytes = std::vector<uint8_t>(data, data + consumed);
  return consumed;
}

size_t Disassembler::DisassembleMany(const uint8_t* data, size_t data_len,
                                     uint64_t start_address,
                                     const Options& in_options,
                                     size_t max_instructions,
                                     std::vector<Row>* out) const {
  if (max_instructions == 0)
    max_instructions = std::numeric_limits<size_t>::max();

  // Force emit_undecodable to true or we can never advance past undecodable
  // instructions.
  Options options = in_options;
  options.emit_undecodable = true;

  size_t byte_offset = 0;
  while (byte_offset < data_len && out->size() < max_instructions) {
    out->emplace_back();
    size_t bytes_read =
        DisassembleOne(&data[byte_offset], data_len - byte_offset,
                       start_address + byte_offset, options, &out->back());
    FXL_DCHECK(bytes_read > 0);
    byte_offset += bytes_read;
  }

  return byte_offset;
}

size_t Disassembler::DisassembleDump(const MemoryDump& dump,
                                     uint64_t start_address,
                                     const Options& options,
                                     size_t max_instructions,
                                     std::vector<Row>* out) const {
  if (max_instructions == 0)
    max_instructions = std::numeric_limits<size_t>::max();

  uint64_t cur_address = start_address;
  for (size_t block_i = 0; block_i < dump.blocks().size(); block_i++) {
    const debug_ipc::MemoryBlock& block = dump.blocks()[block_i];

    uint64_t block_end = block.address + block.size;
    if (cur_address >= block_end)
      continue;  // Not in this block.

    if (!block.valid) {
      // Invalid region.
      std::string comment =
          arch_->asm_info()->getCommentString().str() + " Invalid memory @ ";

      if (block_i == dump.blocks().size() - 1) {
        // If the last block, just show the starting address because the size
        // will normally be irrelevant (say disassembling at the current IP
        // which might be invalid -- the user doesn't care how big the
        // invalid memory region is, or how much was requested).
        comment += fxl::StringPrintf("0x%" PRIx64, block.address);
      } else {
        // Invalid range.
        comment +=
            fxl::StringPrintf("0x%" PRIx64 " - 0x%" PRIx64, block.address,
                              block.address + block.size - 1);
      }

      // Append the row.
      out->emplace_back();
      Row& row = out->back();
      row.address = block.address;
      row.op = "??";
      row.comment = std::move(comment);

      cur_address = block_end;
      continue;
    }

    uint64_t block_offset = cur_address - block.address;
    if (block_offset < block.data.size()) {
      // Valid region, print instructions to the end of the block.
      size_t block_bytes_consumed = DisassembleMany(
          &block.data[block_offset], block.data.size(),
          block.address + block_offset, options, max_instructions, out);
      if (out->size() >= max_instructions) {
        // Return the number of bytes from the beginning of the memory dump
        // that were consumed.
        return static_cast<size_t>(block.address + block_bytes_consumed -
                                   dump.blocks()[0].address);
      }
    }
    cur_address = block_end;
  }

  // All bytes of the memory dump were consumed.
  return static_cast<size_t>(dump.size());
}

}  // namespace zxdb
