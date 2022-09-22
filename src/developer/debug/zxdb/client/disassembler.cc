// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/disassembler.h"

#include <inttypes.h>

#include <limits>
#include <ostream>

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/TargetRegistry.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

// In-place replaces instances of ANY of the characters in "search_for" with the given replacement
// in the given string.
void ReplaceAllInstancesOf(const char* search_for, char replace_with, std::string* str) {
  size_t found_pos = 0;
  while ((found_pos = str->find_first_of(search_for, found_pos)) != std::string::npos) {
    (*str)[found_pos] = replace_with;
  }
}

void GetInvalidInstructionStrs(const uint8_t* data, size_t len, std::string* instruction,
                               std::string* params, std::string* comment) {
  *instruction = ".byte";
  params->clear();
  for (size_t i = 0; i < len; i++) {
    if (i > 0)
      params->push_back(' ');
    params->append(fxl::StringPrintf("0x%2.2x", data[i]));
  }
  *comment = "Invalid instruction.";
}

// LLVM generates a instructions like "\tmov\ta,b". Given a string like this with two tabs in the
// instruction input, separates the parameters ("a,b") off into the given params string, and strips
// tabs leaving only the instruction ("mov") in the input string.
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
  instruction->erase(instruction->begin() + param_separator, instruction->end());
}

}  // namespace

Disassembler::Row::Row() = default;

Disassembler::Row::Row(uint64_t address, const uint8_t* bytes, size_t bytes_len, std::string op,
                       std::string params, std::string comment, InstructionType type,
                       std::optional<uint64_t> call_dest)
    : address(address),
      bytes(bytes, bytes + bytes_len),
      op(op),
      params(params),
      comment(comment),
      type(type),
      call_dest(call_dest) {}

Disassembler::Row::~Row() = default;

bool Disassembler::Row::operator==(const Row& other) const {
  return address == other.address && bytes == other.bytes && op == other.op &&
         params == other.params && comment == other.comment && type == other.type &&
         call_dest == other.call_dest;
}

std::ostream& operator<<(std::ostream& out, const Disassembler::Row& row) {
  out << to_hex_string(row.address) << "\t";

  for (size_t i = 0; i < row.bytes.size(); i++) {
    if (i > 0)
      out << " ";
    out << to_hex_string(row.bytes[i], 2, false);
  }

  out << "\t" << row.op << "\t" << row.params;

  if (!row.comment.empty()) {
    out << "\t# " << row.comment;
  } else if (row.type != Disassembler::InstructionType::kOther) {
    out << "\t#";
  }

  switch (row.type) {
    case Disassembler::InstructionType::kCallDirect:
      if (row.call_dest) {
        out << " (call to " << to_hex_string(*row.call_dest) << ")";
      } else {
        out << " (call to unknown)";
      }
      break;
    case Disassembler::InstructionType::kCallIndirect:
      out << " (indirect call)";
      break;
    case Disassembler::InstructionType::kOther:
      break;
  }

  return out;
}

Disassembler::Disassembler() = default;
Disassembler::~Disassembler() = default;

Err Disassembler::Init(const ArchInfo* arch) {
  arch_ = arch;

  context_ = std::make_unique<llvm::MCContext>(*arch_->triple(), arch_->asm_info(),
                                               arch_->register_info(), nullptr);
  disasm_.reset(arch_->target()->createMCDisassembler(*arch_->subtarget_info(), *context_));
  if (!disasm_)
    return Err("Couldn't create LLVM disassembler.");

  constexpr int kAssemblyFlavor = 1;  // 1 means "Intel" (not AT&T).
  printer_.reset(arch_->target()->createMCInstPrinter(*arch_->triple(), kAssemblyFlavor,
                                                      *arch_->asm_info(), *arch_->instr_info(),
                                                      *arch_->register_info()));
  printer_->setPrintHexStyle(llvm::HexStyle::C);  // ::C = 0xff-style.
  printer_->setPrintImmHex(true);
  printer_->setUseMarkup(false);

  return Err();
}

size_t Disassembler::DisassembleOne(const uint8_t* data, size_t data_len, uint64_t address,
                                    const Options& options, Row* out) const {
  out->address = address;

  // Decode.
  llvm::MCInst inst;
  uint64_t consumed = 0;
  auto status = disasm_->getInstruction(inst, consumed, llvm::ArrayRef<uint8_t>(data, data_len),
                                        address, llvm::nulls());
  if (status == llvm::MCDisassembler::Success) {
    // Print the instruction. Note that LLVM appends to the strings so we need to make sure they're
    // empty before using.
    out->op.clear();
    out->comment.clear();
    llvm::raw_string_ostream inst_stream(out->op);
    llvm::raw_string_ostream comment_stream(out->comment);

    printer_->setCommentStream(comment_stream);
    printer_->printInst(&inst, 0, llvm::StringRef(), *arch_->subtarget_info(), inst_stream);
    printer_->setCommentStream(llvm::nulls());

    inst_stream.flush();
    comment_stream.flush();

    SplitInstruction(&out->op, &out->params);
    FillInstructionInfo(address, data, consumed, inst, out);
  } else {
    // Failure decoding.
    if (!options.emit_undecodable)
      return 0;
    consumed = std::min(data_len, arch_->instr_align());
    GetInvalidInstructionStrs(data, consumed, &out->op, &out->params, &out->comment);
  }

  // Comments.
  if (!out->comment.empty()) {
    // Canonicalize the comments, they'll end in a newline (which is added manually later) and may
    // contain embedded newlines.
    out->comment = fxl::TrimString(out->comment, "\r\n ");
    ReplaceAllInstancesOf("\r\n", ' ', &out->comment);

    out->comment = arch_->asm_info()->getCommentString().str() + " " + out->comment;
  }

  out->bytes = std::vector<uint8_t>(data, data + consumed);
  return consumed;
}

size_t Disassembler::DisassembleMany(const uint8_t* data, size_t data_len, uint64_t start_address,
                                     const Options& in_options, size_t max_instructions,
                                     std::vector<Row>* out) const {
  if (max_instructions == 0)
    max_instructions = std::numeric_limits<size_t>::max();

  // Force emit_undecodable to true or we can never advance past undecodable instructions.
  Options options = in_options;
  options.emit_undecodable = true;

  size_t byte_offset = 0;
  while (byte_offset < data_len && out->size() < max_instructions) {
    out->emplace_back();
    size_t bytes_read = DisassembleOne(&data[byte_offset], data_len - byte_offset,
                                       start_address + byte_offset, options, &out->back());
    FX_DCHECK(bytes_read > 0);
    byte_offset += bytes_read;
  }

  return byte_offset;
}

size_t Disassembler::DisassembleDump(const MemoryDump& dump, uint64_t start_address,
                                     const Options& options, size_t max_instructions,
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
      std::string comment = arch_->asm_info()->getCommentString().str() + " Invalid memory @ ";

      if (block_i == dump.blocks().size() - 1) {
        // If the last block, just show the starting address because the size will normally be
        // irrelevant (say disassembling at the current IP which might be invalid -- the user
        // doesn't care how big the invalid memory region is, or how much was requested).
        comment += fxl::StringPrintf("0x%" PRIx64, block.address);
      } else {
        // Invalid range.
        comment += fxl::StringPrintf("0x%" PRIx64 " - 0x%" PRIx64, block.address,
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
      size_t block_bytes_consumed =
          DisassembleMany(&block.data[block_offset], block.data.size(),
                          block.address + block_offset, options, max_instructions, out);
      if (out->size() >= max_instructions) {
        // Return the number of bytes from the beginning of the memory dump that were consumed.
        return static_cast<size_t>(block.address + block_bytes_consumed - dump.blocks()[0].address);
      }
    }
    cur_address = block_end;
  }

  // All bytes of the memory dump were consumed.
  return static_cast<size_t>(dump.size());
}

void Disassembler::FillInstructionInfo(uint64_t address, const uint8_t* data, uint64_t data_len,
                                       const llvm::MCInst& inst, Row* row) const {
  row->type = InstructionType::kOther;  // Default to "other" for early returns below.

  // inst.getOpcode() returns an LLVM enum value that's defined in an internal header not included
  // in our build. Therefore, this can not be used and the raw instruction bytes are checked
  // instead.
  if (inst.getNumOperands() != 1)
    return;  // All instructions we care about have one operand.
  const llvm::MCOperand& operand = inst.getOperand(0);

  if (arch_->arch() == debug::Arch::kX64) {
    // On x64, almost all of our calls use the 32-bit instruction-relative variant. Most of the
    // other variants are indirect so can't be decoded statically. Therefore this is the only
    // variant we're worrying about here.
    if (data_len >= 1 && data[0] == 0xe8) {  // call _rel32_
      // Opcode has one operand which is a 32-bit signed offset from the address of the next
      // instruction.
      if (!operand.isImm())
        return;  // Invalid.

      row->type = InstructionType::kCallDirect;
      row->call_dest = address + 5 /* length of instruction */ + operand.getImm();
      return;
    }

    // Indirect calls are listed as:
    //   Opcode byte   Mod R/M byte
    //   11111111      ..010...      Near call "FF /2"
    //   11111111      ..011...      Far call "FF /3"
    constexpr uint8_t kModRMByteRegOpcodeMask = 0b00111000;
    constexpr uint8_t kNearCallModRMValue = 0b00010000;
    constexpr uint8_t kFarCallModRMValue = 0b00011000;
    if (data_len >= 2 && data[0] == 0xff &&
        ((data[1] & kModRMByteRegOpcodeMask) == kNearCallModRMValue ||
         (data[1] & kModRMByteRegOpcodeMask) == kFarCallModRMValue)) {
      row->type = InstructionType::kCallIndirect;
      return;
    }
  } else if (arch_->arch() == debug::Arch::kArm64 && data_len == sizeof(uint32_t)) {
    // The BL instruction has the high 6 bits 0b100101 (in data[3] for little-endian).
    if ((data[3] & 0b11111100) == 0b10010100) {
      // Opcode has one operand which is a 24-bit signed offset from the address of this
      // instruction, divided by 4.
      if (!operand.isImm())
        return;  // Invalid.

      row->type = InstructionType::kCallDirect;
      row->call_dest = address + operand.getImm() * 4;
      return;
    }

    uint32_t instruction;
    memcpy(&instruction, data, sizeof(uint32_t));

    // The BLR instruction (Branch and Link to Register value) has the encoding:
    //  3         2         1         0
    // 10987654321098765432109876543210
    // --------------------------------
    // 1101011000111111000000.....00000
    //                         ^---- destination register
    constexpr uint32_t kBLRMask = 0b11111111'11111111'11111100'00011111;
    constexpr uint32_t kBLRInst = 0b11010110'00111111'00000000'00000000;
    if ((instruction & kBLRMask) == kBLRInst) {
      row->type = InstructionType::kCallIndirect;
      return;
    }
  }
}

}  // namespace zxdb
