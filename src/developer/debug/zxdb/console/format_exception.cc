// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_exception.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/console_context.h"

namespace zxdb {

namespace {

std::string X64PageFaultToString(const debug_ipc::ExceptionRecord& record) {
  // Bits in the error code for a page fault.
  constexpr uint32_t kPresentBit = 1 << 0;
  constexpr uint32_t kWriteBit = 1 << 1;
  // constexpr uint32_t kUserBit = 1 << 2;  // Currently unneeded.
  // constexpr uint32_t kReservedWrite = 1 << 3;  // Currently unneeded.
  constexpr uint32_t kInstructionFetchBit = 1 << 4;

  std::string result = "Page fault ";

  // Decode read/write/execute.
  if (record.arch.x64.err_code & kInstructionFetchBit)
    result += "executing ";
  else if (record.arch.x64.err_code & kWriteBit)
    result += "writing ";
  else
    result += "reading ";

  result += "address ";
  result += to_hex_string(record.arch.x64.cr2);

  // The page table can mark pages as explicitly protected. Otherwise the page isn't in the page
  // table.
  if (record.arch.x64.err_code & kPresentBit)
    result += " (page protection violation)";

  return result;
}

std::string X64ExceptionRecordToString(const debug_ipc::ExceptionRecord& record) {
  switch (record.arch.x64.vector) {
    // clang-format off
    case  0: return "Divide-by-zero exception";
    case  1: return "Debug exception";
    case  2: return "Non-maskable interrupt";
    case  3: return "Breakpoint exception";
    case  4: return "Overflow exception";
    case  5: return "Bound range exceeded exception";
    case  6: return "Invalid opcode exception";
    case  7: return "No math coprocessor present exception";
    case  8: return "Double fault";
    case  9: return "CoProcessor segment overrun exception";
    case 10: return "Invalid TSS exception";
    case 11: return "Segment not present exception";
    case 12: return "Stack segment fault";
    case 13: return "General protection fault";
    case 14: return X64PageFaultToString(record);
    case 15: return "Reserved exception";
    case 16: return "Floating-point exception";
    case 17: return "Alignment check exception";
    case 18: return "Machine check exception";
    case 19: return "SIMD floating-point exception";
    case 20: return "Virtualization exception";
    case 21: return "Control protection exception";
    // clang-format on
    default:
      return "Unknown exception (" + std::to_string(record.arch.x64.vector) + ")";
  }
}

std::string Arm64DataAbortToString(const debug_ipc::ExceptionRecord& record) {
  constexpr uint32_t kWriteNotReadBit = 1 << 6;

  // Toplevel description.
  std::string result = "Data fault ";
  if (record.arch.arm64.esr & kWriteNotReadBit)
    result += "writing ";
  else
    result += "reading ";

  result += "address ";
  result += to_hex_string(record.arch.arm64.far);

  // The data fault status code is the low 6 bits of the ESR.
  // Many of these we'll never see but it's easier to make the table complete.
  uint32_t dfsc = record.arch.arm64.esr & 0b111111;
  const char* status = nullptr;
  switch (dfsc) {
    // clang-format off
    case 0b000000: status = "address size fault level 0"; break;
    case 0b000001: status = "address size fault level 1"; break;
    case 0b000010: status = "address size fault level 2"; break;
    case 0b000011: status = "address size fault level 3"; break;
    case 0b000100: status = "translation fault level 0"; break;
    case 0b000101: status = "translation fault level 1"; break;
    case 0b000110: status = "translation fault level 2"; break;
    case 0b000111: status = "translation fault level 3"; break;
    case 0b001001: status = "access fault level 1"; break;
    case 0b001010: status = "access fault level 2"; break;
    case 0b001011: status = "access fault level 3"; break;
    case 0b001101: status = "permission fault level 1"; break;
    case 0b001110: status = "permission fault level 2"; break;
    case 0b001111: status = "permission fault level 3"; break;
    case 0b010000: status = "external, not on translation table walk"; break;
    case 0b010001: status = "synchronous tag check fail"; break;
    case 0b010100: status = "external, on translation table walk level 0"; break;
    case 0b010101: status = "external, on translation table walk level 1"; break;
    case 0b010110: status = "external, on translation table walk level 2"; break;
    case 0b010111: status = "external, on translation table walk level 3"; break;
    case 0b011000: status = "parity/ECC error not on translation table walk"; break;
    case 0b011100: status = "parity/ECC error on translation table walk level 0"; break;
    case 0b011101: status = "parity/ECC error on translation table walk level 1"; break;
    case 0b011110: status = "parity/ECC error on translation table walk level 2"; break;
    case 0b011111: status = "parity/ECC error on translation table walk level 3"; break;
    case 0b100001: status = "alignment fault"; break;
    case 0b110000: status = "TLB conflict"; break;
    case 0b110001: status = "unsupported atomic hardware updated"; break;
    case 0b110100: status = "implementation defined - lockdown"; break;
    case 0b110101: status = "implementation defined - unsupported exclusive or atomic"; break;
    case 0b111101: status = "section domain fault"; break;
    case 0b111110: status = "page domain fault"; break;
      // clang-format on
  }
  if (status) {
    result += " (";
    result += status;
    result += ")";
  }

  return result;
}

std::string Arm64ExceptionRecordToString(const debug_ipc::ExceptionRecord& record) {
  // The exception class is bits 26-31 in the esr register.
  uint32_t ec = (record.arch.arm64.esr >> 26) & 0b111111;

  // This is the list from:
  // https://developer.arm.com/docs/ddi0595/e/aarch64-system-registers/esr_el1
  // Many of these we will never encounter at the user level but it's safer to be exhaustive.
  switch (ec) {
    // clang-format off
    case 0b000000: return "Unknown exception";
    case 0b000001: return "Trapped WFI or WFE execution";
    case 0b000011: return "Wrapped MCR or MRC access";
    case 0b000100: return "Trapped MCRR or MRRC";
    case 0b000101: return "Trapped MCR or MRC access";
    case 0b000110: return "Trapped LDC or STC access";
    case 0b000111: return "SVE/SIMD/FP exception";
    case 0b001100: return "Trapped MRRC exception";
    case 0b001101: return "Branch target exception";
    case 0b001110: return "Illegal execution state exception";
    case 0b010001: // fall through
    case 0b010101: return "SVC instruction execution";
    case 0b011000: return "Wrapped MSR, MRS, or system instruction exception";
    case 0b011001: return "Access to SVE exception";
    case 0b011100: return "Pointer authentication failure exception";
    case 0b100000: // fall through
    case 0b100001: return "Instruction abort (MMU fault)";
    case 0b100010: return "PC alignment fault exception";
    case 0b100100: // fall through
    case 0b100101: return Arm64DataAbortToString(record);
    case 0b100110: return "SP alignment fault exception";
    case 0b101000: // fall through
    case 0b101100: return "Wrapped floating-point exception";
    case 0b101111: return "SError interrupt";
    case 0b110000: // fall through
    case 0b110001: return "Breakpoint exception";
    case 0b110010: // fall through
    case 0b110011: return "Software step exception";
    case 0b110100: // fall through
    case 0b110101: return "Watchpoint exception";
    case 0b111000: return "BKPT instruction";
    case 0b111100: return "BRK instruction";
      // clang-format on
  }
  return std::string();
}

}  // namespace

OutputBuffer FormatException(const ConsoleContext* context, const Thread* thread,
                             const debug_ipc::ExceptionRecord& record) {
  std::string heading = ExceptionRecordToString(thread->session()->arch(), record);

  // Lines on each side of the exception string. Max out at 80 cols in the case of long strings.
  // Leave two extra to indent the string a bit.
  size_t divider_length = std::min<size_t>(heading.size() + 2, 80);
  std::string divider;
  for (size_t i = 0; i < divider_length; i++)
    divider += "═";  // UTF-8 character, can't use the string repeated constructor.

  OutputBuffer out;
  out.Append(Syntax::kError, divider);
  out.Append("\n ");  // Extra space to indent heading inside dividers.
  out.Append(Syntax::kHeading, heading);
  out.Append("\n");
  out.Append(Syntax::kError, divider);
  out.Append("\n");

  // Output process record.
  out.Append(" Process ");
  out.Append(Syntax::kSpecial,
             std::to_string(context->IdForTarget(thread->GetProcess()->GetTarget())));
  out.Append(" (");
  out.Append(Syntax::kVariable, "koid");
  out.Append("=");
  out.Append(std::to_string(thread->GetProcess()->GetKoid()));
  out.Append(") ");

  // Output thread record.
  out.Append("thread ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForThread(thread)));
  out.Append(" (");
  out.Append(Syntax::kVariable, "koid");
  out.Append("=");
  out.Append(std::to_string(thread->GetKoid()));
  out.Append(")\n");

  // Output exception address.
  const Stack& stack = thread->GetStack();
  if (!stack.empty()) {
    out.Append(" Faulting instruction: " + to_hex_string(stack[0]->GetAddress()));
    out.Append("\n");
  }

  return out;
}

std::string ExceptionRecordToString(debug_ipc::Arch arch,
                                    const debug_ipc::ExceptionRecord& record) {
  if (!record.valid)
    return "No exception information";

  std::string suffix =
      (record.strategy == debug_ipc::ExceptionStrategy::kSecondChance) ? " (second chance)" : "";
  switch (arch) {
    case debug_ipc::Arch::kUnknown:
      return "Unknown architecture";
    case debug_ipc::Arch::kX64:
      return X64ExceptionRecordToString(record) + suffix;
    case debug_ipc::Arch::kArm64:
      return Arm64ExceptionRecordToString(record) + suffix;
  }
  FX_NOTREACHED();
  return std::string();
}

}  // namespace zxdb
