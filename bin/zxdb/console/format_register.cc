// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <regex.h>

#include <map>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "garnet/public/lib/fxl/functional/auto_call.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

using debug_ipc::RegisterID;

namespace {

inline OutputBuffer RegisterValueToOutputBuffer(const Register& reg) {
  if (reg.data().empty()) {
    FXL_NOTREACHED() << "Invalid size for " << __PRETTY_FUNCTION__ << ": "
                     << reg.size();
  }

  // For now we print into chunks of 32-bits, shortening the ends.
  // TODO(donosoc): Extract this logic to be used separatedly by sub-registers.
  // TODO(donosoc): FP formatting: float, double-float, extended double-float.
  // TODO(donosoc): Vector formatting.
  auto cur = reg.begin();
  auto end = reg.end();
  std::vector<std::string> chunks;
  while (cur < end) {
    auto diff = end - cur;
    if (diff >= 4) {
      auto* val = reinterpret_cast<const uint32_t*>(&*cur);
      chunks.push_back(fxl::StringPrintf("%.8x", *val));
      cur += 4;
      continue;
    }

    switch (diff) {
      case 1: {
        auto* val = reinterpret_cast<const uint8_t*>(&*cur);
        chunks.push_back(fxl::StringPrintf("%.2x", *val));
        cur += diff;
        continue;
      }
      case 2: {
        auto* val = reinterpret_cast<const uint16_t*>(&*cur);
        chunks.push_back(fxl::StringPrintf("%.4x", *val));
        cur += diff;
        continue;
      }
    }

    FXL_NOTREACHED() << "Invalid size for " << __PRETTY_FUNCTION__ << ": "
                     << reg.size();
  }

  // Though each particular chunk correctly keeps the endianness, the order is
  // backwards, as the last bits of the register would be printed first.
  // For that we append the chunks backwards.
  OutputBuffer out;
  auto cit = chunks.rbegin();
  while (cit != chunks.rend()) {
    out.Append(*cit);
    cit++;
    if (cit != chunks.rend())
      out.Append(" ");
  }
  return out;
}

inline Err RegexpError(const char* prefix, const std::string& pattern,
                       const regex_t* regexp, int status) {
  char err_buf[256];
  regerror(status, regexp, err_buf, sizeof(err_buf));
  return Err(
      fxl::StringPrintf("%s \"%s\": %s", prefix, pattern.c_str(), err_buf));
}

// Using a vector of output buffers make it easy to not have to worry about
// appending new lines per each new section.
Err InternalFormatCategory(debug_ipc::RegisterCategory::Type cat,
                           const std::vector<Register>& registers,
                           const std::string& search_regexp,
                           std::vector<OutputBuffer>* out_buffers) {
  std::vector<std::pair<const Register*, std::string>> found_registers;
  if (search_regexp.empty()) {
    for (const auto& reg: registers) {
      std::string reg_name = RegisterIDToString(reg.id());
      found_registers.push_back({&reg, std::move(reg_name)});
    }
  } else {
    // We use insensitive case regexp matching.
    regex_t regexp;
    auto status = regcomp(&regexp, search_regexp.c_str(), REG_ICASE);
    fxl::AutoCall<std::function<void()>> reg_freer(
        [&regexp]() { regfree(&regexp); });

    if (status) {
      return RegexpError("Could not compile regexp", search_regexp.c_str(),
                         &regexp, status);
    }

    for (const auto& reg : registers) {
      const char* reg_name = RegisterIDToString(reg.id());
      // We don't care about the matches.
      status = regexec(&regexp, reg_name, 0, nullptr, 0);
      if (!status) {
        found_registers.push_back({&reg, std::move(reg_name)});
      } else if (status != REG_NOMATCH) {
        return RegexpError("Error running regexp", search_regexp.c_str(),
                           &regexp, status);
      }
    }
  }

  // If this category didn't find registers, skip it.
  if (found_registers.empty())
    return Err();

  // Title.
  auto category_title = fxl::StringPrintf(
      "%s Registers", RegisterCategoryTypeToString(cat).c_str());
  out_buffers->push_back(
      OutputBuffer::WithContents(Syntax::kHeading, category_title));

  // Registers.
  std::vector<std::vector<OutputBuffer>> rows;
  for (const auto reg_pair : found_registers) {
    rows.emplace_back();
    auto& row = rows.back();

    row.push_back(OutputBuffer::WithContents(reg_pair.second));
    row.push_back(OutputBuffer::WithContents(
        fxl::StringPrintf("%zu", reg_pair.first->size())));
    row.push_back(RegisterValueToOutputBuffer(*reg_pair.first));
    if (cat == debug_ipc::RegisterCategory::Type::kFloatingPoint) {
      std::string out;
      if(GetFPString(*reg_pair.first, &out).ok())
        row.push_back(OutputBuffer::WithContents(std::move(out)));
      else
        row.push_back({});
    }
  }

  out_buffers->push_back({});
  OutputBuffer& out = out_buffers->back();
  auto colspecs = std::vector<ColSpec>(
      {ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kRight, 0, "Size"),
       ColSpec(Align::kRight, 0, "Value", 2)});

  if (cat == debug_ipc::RegisterCategory::Type::kFloatingPoint) {
    colspecs.push_back(ColSpec(Align::kRight, 0, "FP"));
  }
  FormatTable(colspecs, rows, &out);

  return Err();
}

}  // namespace

Err FormatRegisters(const RegisterSet& registers,
                    const std::string& searched_register, OutputBuffer* out,
                    std::vector<debug_ipc::RegisterCategory::Type> categories) {
  std::vector<OutputBuffer> out_buffers;
  const auto& category_map = registers.category_map();

  // Go category to category trying to print.
  for (const auto& category : categories) {
    auto it = category_map.find(category);
    if (it == category_map.end()) {
      continue;
    }

    Err err = InternalFormatCategory(category, it->second, searched_register,
                                     &out_buffers);
    if (err.has_error()) {
      return err;
    }
  }
  // If nothing was printed, it means that we couldn't find the register.
  if (out_buffers.empty()) {
    if (searched_register.empty()) {
      return Err("No registers to show in the selected categories");
    } else {
      return Err(fxl::StringPrintf(
          "Could not find register \"%s\" in the selected categories",
          searched_register.c_str()));
    }
  }

  // Each section is separated by a new line.
  for (const auto& buf : out_buffers) {
    out->Append(buf);
    out->Append("\n");
  }

  return Err();
}

// Formatting helpers ----------------------------------------------------------

std::string RegisterCategoryTypeToString(
    debug_ipc::RegisterCategory::Type type) {
  switch (type) {
    case debug_ipc::RegisterCategory::Type::kGeneral:
      return "General Purpose";
    case debug_ipc::RegisterCategory::Type::kFloatingPoint:
      return "Floating Point";
    case debug_ipc::RegisterCategory::Type::kVector:
      return "Vector";
    case debug_ipc::RegisterCategory::Type::kMisc:
      return "Miscellaneous";
  }
}

const char* RegisterIDToString(RegisterID id) {
  switch (id) {
    case RegisterID::kUnknown:
      break;

    // ARMv8 -------------------------------------------------------------------

    // General purpose.

    case RegisterID::kARMv8_x0:   return "x0";
    case RegisterID::kARMv8_x1:   return "x1";
    case RegisterID::kARMv8_x2:   return "x2";
    case RegisterID::kARMv8_x3:   return "x3";
    case RegisterID::kARMv8_x4:   return "x4";
    case RegisterID::kARMv8_x5:   return "x5";
    case RegisterID::kARMv8_x6:   return "x6";
    case RegisterID::kARMv8_x7:   return "x7";
    case RegisterID::kARMv8_x8:   return "x8";
    case RegisterID::kARMv8_x9:   return "x9";
    case RegisterID::kARMv8_x10:  return "x10";
    case RegisterID::kARMv8_x11:  return "x11";
    case RegisterID::kARMv8_x12:  return "x12";
    case RegisterID::kARMv8_x13:  return "x13";
    case RegisterID::kARMv8_x14:  return "x14";
    case RegisterID::kARMv8_x15:  return "x15";
    case RegisterID::kARMv8_x16:  return "x16";
    case RegisterID::kARMv8_x17:  return "x17";
    case RegisterID::kARMv8_x18:  return "x18";
    case RegisterID::kARMv8_x19:  return "x19";
    case RegisterID::kARMv8_x20:  return "x20";
    case RegisterID::kARMv8_x21:  return "x21";
    case RegisterID::kARMv8_x22:  return "x22";
    case RegisterID::kARMv8_x23:  return "x23";
    case RegisterID::kARMv8_x24:  return "x24";
    case RegisterID::kARMv8_x25:  return "x25";
    case RegisterID::kARMv8_x26:  return "x26";
    case RegisterID::kARMv8_x27:  return "x27";
    case RegisterID::kARMv8_x28:  return "x28";
    case RegisterID::kARMv8_x29:  return "x29";
    case RegisterID::kARMv8_lr:   return "lr";
    case RegisterID::kARMv8_sp:   return "sp";
    case RegisterID::kARMv8_pc:   return "pc";
    case RegisterID::kARMv8_cpsr: return "cpsr";

    // FP (none defined for ARM64).

    // Vector.

    case RegisterID::kARMv8_fpcr: return "fpcr";
    case RegisterID::kARMv8_fpsr: return "fpsr";

    case RegisterID::kARMv8_v0:   return "v0";
    case RegisterID::kARMv8_v1:   return "v1";
    case RegisterID::kARMv8_v2:   return "v2";
    case RegisterID::kARMv8_v3:   return "v3";
    case RegisterID::kARMv8_v4:   return "v4";
    case RegisterID::kARMv8_v5:   return "v5";
    case RegisterID::kARMv8_v6:   return "v6";
    case RegisterID::kARMv8_v7:   return "v7";
    case RegisterID::kARMv8_v8:   return "v8";
    case RegisterID::kARMv8_v9:   return "v9";
    case RegisterID::kARMv8_v10:  return "v10";
    case RegisterID::kARMv8_v11:  return "v11";
    case RegisterID::kARMv8_v12:  return "v12";
    case RegisterID::kARMv8_v13:  return "v13";
    case RegisterID::kARMv8_v14:  return "v14";
    case RegisterID::kARMv8_v15:  return "v15";
    case RegisterID::kARMv8_v16:  return "v16";
    case RegisterID::kARMv8_v17:  return "v17";
    case RegisterID::kARMv8_v18:  return "v18";
    case RegisterID::kARMv8_v19:  return "v19";
    case RegisterID::kARMv8_v20:  return "v20";
    case RegisterID::kARMv8_v21:  return "v21";
    case RegisterID::kARMv8_v22:  return "v22";
    case RegisterID::kARMv8_v23:  return "v23";
    case RegisterID::kARMv8_v24:  return "v24";
    case RegisterID::kARMv8_v25:  return "v25";
    case RegisterID::kARMv8_v26:  return "v26";
    case RegisterID::kARMv8_v27:  return "v27";
    case RegisterID::kARMv8_v28:  return "v28";
    case RegisterID::kARMv8_v29:  return "v29";
    case RegisterID::kARMv8_v30:  return "v30";
    case RegisterID::kARMv8_v31:  return "v31";

    // x64 ---------------------------------------------------------------------

    // General purpose.

    case RegisterID::kX64_rax:    return "rax";
    case RegisterID::kX64_rbx:    return "rbx";
    case RegisterID::kX64_rcx:    return "rcx";
    case RegisterID::kX64_rdx:    return "rdx";
    case RegisterID::kX64_rsi:    return "rsi";
    case RegisterID::kX64_rdi:    return "rdi";
    case RegisterID::kX64_rbp:    return "rbp";
    case RegisterID::kX64_rsp:    return "rsp";
    case RegisterID::kX64_r8:     return "r8";
    case RegisterID::kX64_r9:     return "r9";
    case RegisterID::kX64_r10:    return "r10";
    case RegisterID::kX64_r11:    return "r11";
    case RegisterID::kX64_r12:    return "r12";
    case RegisterID::kX64_r13:    return "r13";
    case RegisterID::kX64_r14:    return "r14";
    case RegisterID::kX64_r15:    return "r15";
    case RegisterID::kX64_rip:    return "rip";
    case RegisterID::kX64_rflags: return "rflags";

    // FP.

    case RegisterID::kX64_fcw: return "fcw";
    case RegisterID::kX64_fsw: return "fsw";
    case RegisterID::kX64_ftw: return "ftw";
    case RegisterID::kX64_fop: return "fop";
    case RegisterID::kX64_fip: return "fip";
    case RegisterID::kX64_fdp: return "fdp";

    case RegisterID::kX64_st0: return "st0";
    case RegisterID::kX64_st1: return "st1";
    case RegisterID::kX64_st2: return "st2";
    case RegisterID::kX64_st3: return "st3";
    case RegisterID::kX64_st4: return "st4";
    case RegisterID::kX64_st5: return "st5";
    case RegisterID::kX64_st6: return "st6";
    case RegisterID::kX64_st7: return "st7";

    // Vector.

    case RegisterID::kX64_mxcsr : return "mxcsr";

    // SSE/SSE2 (128 bit).
    case RegisterID::kX64_xmm0:   return "m0";
    case RegisterID::kX64_xmm1:   return "m1";
    case RegisterID::kX64_xmm2:   return "m2";
    case RegisterID::kX64_xmm3:   return "m3";
    case RegisterID::kX64_xmm4:   return "m4";
    case RegisterID::kX64_xmm5:   return "m5";
    case RegisterID::kX64_xmm6:   return "m6";
    case RegisterID::kX64_xmm7:   return "m7";
    case RegisterID::kX64_xmm8:   return "m8";
    case RegisterID::kX64_xmm9:   return "m9";
    case RegisterID::kX64_xmm10:  return "m10";
    case RegisterID::kX64_xmm11:  return "m11";
    case RegisterID::kX64_xmm12:  return "m12";
    case RegisterID::kX64_xmm13:  return "m13";
    case RegisterID::kX64_xmm14:  return "m14";
    case RegisterID::kX64_xmm15:  return "m15";

    // AVX (256 bit).
    case RegisterID::kX64_ymm0:   return "m0";
    case RegisterID::kX64_ymm1:   return "m1";
    case RegisterID::kX64_ymm2:   return "m2";
    case RegisterID::kX64_ymm3:   return "m3";
    case RegisterID::kX64_ymm4:   return "m4";
    case RegisterID::kX64_ymm5:   return "m5";
    case RegisterID::kX64_ymm6:   return "m6";
    case RegisterID::kX64_ymm7:   return "m7";
    case RegisterID::kX64_ymm8:   return "m8";
    case RegisterID::kX64_ymm9:   return "m9";
    case RegisterID::kX64_ymm10:  return "m10";
    case RegisterID::kX64_ymm11:  return "m11";
    case RegisterID::kX64_ymm12:  return "m12";
    case RegisterID::kX64_ymm13:  return "m13";
    case RegisterID::kX64_ymm14:  return "m14";
    case RegisterID::kX64_ymm15:  return "m15";

    // TODO(donosoc): Add support for AVX-512 when zircon supports it.
  }

  FXL_NOTREACHED() << "Unknown register requested.";
  return "";
}

}   // namespace zxdb
