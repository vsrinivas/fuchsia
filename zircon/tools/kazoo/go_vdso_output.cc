// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

namespace {

void PrintStub(Writer* writer, Syscall* syscall) {
  writer->Printf("func vdsoCall_zx_%s(", syscall->snake_name().c_str());
  for (size_t i = 0; i < syscall->num_kernel_args(); ++i) {
    if (i > 0) {
      writer->Puts(", ");
    }
    const StructMember& arg = syscall->kernel_arguments()[i];
    writer->Printf("%s %s", RemapReservedGoName(arg.name()).c_str(),
                   GetNativeGoName(arg.type()).c_str());
  }

  writer->Puts(")");
  if (!syscall->is_noreturn() && !syscall->kernel_return_type().IsVoid()) {
    writer->Printf(" %s", GetNativeGoName(syscall->kernel_return_type()).c_str());
  }
  writer->Puts("\n");
}

size_t GoTypeSize(const Type& type) {
  std::string native_name = GetNativeGoName(type);
  if (native_name == "void") {
    return 0;
  }
  if (native_name == "uint8" || native_name == "int8" || native_name == "bool" ||
      native_name == "char") {
    return 1;
  }
  if (native_name == "int16" || native_name == "uint16") {
    return 2;
  }
  if (native_name == "int32" || native_name == "uint32") {
    return 4;
  }
  if (native_name == "uintptr" || native_name == "uint" || native_name == "int64" ||
      native_name == "uint64" || native_name == "unsafe.Pointer") {
    return 8;
  }
  ZX_PANIC("unhandled GoTypeSize: %s", native_name.c_str());
}

enum class Arch {
  kArm64,
  kX86,
};

bool IsSpecialGoRuntimeFunction(const Syscall& syscall) {
  // These functions can't call runtime·entersyscall and exitsyscall, otherwise
  // the system will hang.
  return syscall.name() == "Nanosleep" || syscall.name() == "FutexWait";
}

void PrintAsm(Writer* writer, Syscall* syscall, Arch arch) {
  static const char* kX86RegArgs[] = {"DI", "SI", "DX", "CX", "R8", "R9", "R12", "R13"};
  static const char* kArm64RegArgs[] = {"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7"};

  size_t arg_size = 0;
  for (size_t i = 0; i < syscall->num_kernel_args(); ++i) {
    const StructMember& arg = syscall->kernel_arguments()[i];
    size_t sz = GoTypeSize(arg.type());
    if (arch == Arch::kArm64 && sz == 1) {
      sz = 8;
    }

    while (arg_size % sz != 0) {
      // Add padding until the arg_size is aligned to the type we're adding.
      ++arg_size;
    }
    arg_size += sz;
  }
  if (arg_size % 8 == 4) {
    // Force the return argument on the stack to be 8 byte aligned, not 4.
    arg_size += 4;
  }

  const size_t ret_size = GoTypeSize(syscall->kernel_return_type());

  int frame_size = 0;
  const char** reg_args = nullptr;
  std::string call_ins, ret_reg, suffix4, suffix8;
  switch (arch) {
    case Arch::kX86:
      reg_args = kX86RegArgs;
      call_ins = "CALL";
      ret_reg = "AX";
      suffix8 = "Q";
      suffix4 = "L";
      frame_size = 8;
      if (syscall->num_kernel_args() == 7) {
        frame_size += 16 + 8;
      } else if (syscall->num_kernel_args() == 8) {
        frame_size += 16 + 2 * 8;
      }
      break;
    case Arch::kArm64:
      reg_args = kArm64RegArgs;
      call_ins = "BL";
      ret_reg = "R0";
      suffix8 = "D";
      suffix4 = "W";
      break;
  }

  writer->Printf("TEXT runtime·vdsoCall_zx_%s(SB),NOSPLIT,$%d-%zu\n", syscall->snake_name().c_str(),
                 frame_size, arg_size + ret_size);
  writer->Puts("\tGO_ARGS\n");
  writer->Puts("\tNO_LOCAL_POINTERS\n");

  // Set vdso{PC,SP} so that pprof tracebacks work for VDSO calls.
  switch (arch) {
    case Arch::kX86:
      writer->Puts("\tget_tls(CX)\n");
      writer->Puts("\tMOVQ g(CX), AX\n");
      writer->Puts("\tMOVQ g_m(AX), R14\n");
      writer->Puts("\tPUSHQ R14\n");
      writer->Puts("\tLEAQ ret+0(FP), DX\n");
      writer->Puts("\tMOVQ -8(DX), CX\n");
      writer->Puts("\tMOVQ CX, m_vdsoPC(R14)\n");
      writer->Puts("\tMOVQ DX, m_vdsoSP(R14)\n");
      break;
    case Arch::kArm64:
      writer->Puts("\tMOVD g_m(g), R21\n");
      writer->Puts("\tMOVD LR, m_vdsoPC(R21)\n");

      // If pprof sees the SP value, it will assume the PC value is written and valid. This may not
      // be valid due to store/store reordering on ARM64. This store barrier exists to ensure that
      // any observer of m->vdsoSP is also guaranteed to see m->vdsoPC.
      writer->Puts("\tDMB $0xe\n");
      writer->Puts("\tMOVD $ret-8(FP), R20 // caller's SP\n");
      writer->Puts("\tMOVD R20, m_vdsoSP(R21)\n");
      break;
  }

  if (syscall->HasAttribute("blocking") && !IsSpecialGoRuntimeFunction(*syscall)) {
    writer->Puts("\tCALL runtime·entersyscall(SB)\n");
  }

  size_t off = 0;
  for (size_t i = 0; i < syscall->num_kernel_args(); ++i) {
    const StructMember& arg = syscall->kernel_arguments()[i];
    std::string name = RemapReservedGoName(arg.name());
    std::string suffix = suffix8;
    size_t sz = GoTypeSize(arg.type());
    if (sz == 4) {
      suffix = suffix4;
    } else if (arch == Arch::kArm64 && sz == 1) {
      sz = 8;
    }
    while (off % sz != 0) {
      // Add padding until the offset is aligned to the type we are accessing
      ++off;
    }

    writer->Printf("\tMOV%s %s+%zu(FP), %s\n", suffix.c_str(), name.c_str(), off, reg_args[i]);
    off += sz;
  }

  switch (arch) {
    case Arch::kX86:
      if (syscall->num_kernel_args() >= 7) {
        writer->Puts("\tMOVQ SP, BP   // BP is preserved across vsdo call by the x86-64 ABI\n");
        writer->Puts("\tANDQ $~15, SP // stack alignment for x86-64 ABI\n");
        if (syscall->num_kernel_args() == 8) {
          writer->Puts("\tPUSHQ R13\n");
        }
        writer->Puts("\tPUSHQ R12\n");
      }
      writer->Printf("\tMOVQ vdso_zx_%s(SB), AX\n", syscall->snake_name().c_str());
      writer->Puts("\tCALL AX\n");
      if (syscall->num_kernel_args() >= 7) {
        writer->Puts("\tPOPQ R12\n");
        if (syscall->num_kernel_args() == 8) {
          writer->Puts("\tPOPQ R13\n");
        }
        writer->Puts("\tMOVQ BP, SP\n");
      }
      break;
    case Arch::kArm64:
      writer->Printf("\tBL vdso_zx_%s(SB)\n", syscall->snake_name().c_str());
  }

  if (ret_size > 0) {
    std::string suffix = suffix8;
    if (ret_size == 4) {
      suffix = suffix4;
    }
    writer->Printf("\tMOV%s %s, ret+%zu(FP)\n", suffix.c_str(), ret_reg.c_str(), arg_size);
  }

  if (syscall->HasAttribute("blocking") && !IsSpecialGoRuntimeFunction(*syscall)) {
    writer->Printf("\t%s runtime·exitsyscall(SB)\n", call_ins.c_str());
  }

  // Clear vdsoSP. sigprof only checks vdsoSP for generating tracebacks, so we can leave vdsoPC
  // alone.
  switch (arch) {
    case Arch::kX86:
      writer->Puts("\tPOPQ R14\n");
      writer->Puts("\tMOVQ $0, m_vdsoSP(R14)\n");
      break;
    case Arch::kArm64:
      writer->Puts("\tMOVD g_m(g), R21\n");
      writer->Puts("\tMOVD $0, m_vdsoSP(R21)\n");
      break;
  }
  writer->Puts("\tRET\n");
}

bool VdsoCalls(const SyscallLibrary& library, Writer* writer, Arch arch) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts("#include \"go_asm.h\"\n");
  writer->Puts("#include \"go_tls.h\"\n");
  writer->Puts("#include \"textflag.h\"\n");
  writer->Puts("#include \"funcdata.h\"\n\n");

  for (const auto& syscall : library.syscalls()) {
    writer->Printf("// ");
    PrintStub(writer, syscall.get());
    PrintAsm(writer, syscall.get(), arch);
    writer->Puts("\n");
  }

  return true;
}

}  // namespace

bool GoVdsoKeys(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts("package runtime\n\n");
  writer->Puts("import \"unsafe\"\n\n");
  writer->Puts("const (\n");
  writer->Puts(
      "\t// vdsoArrayMax is the byte-size of a maximally sized array on this architecture.\n");
  writer->Puts("\t// See cmd/compile/internal/amd64/galign.go arch.MAXWIDTH initialization.\n");
  writer->Puts("\tvdsoArrayMax = 1<<50 - 1\n");
  writer->Puts(")\n\n");

  writer->Puts("var vdsoSymbolKeys = []vdsoSymbolKey{\n");
  for (const auto& syscall : library.syscalls()) {
    std::string sym("_zx_" + syscall->snake_name());
    writer->Printf("\t{\"%s\", 0x%x, &vdso%s},\n", sym.c_str(), DJBHash(sym), sym.c_str());
  }
  writer->Puts("}\n");

  writer->Puts("\n");
  for (const auto& syscall : library.syscalls()) {
    writer->Printf("//go:cgo_import_dynamic vdso_zx_%s zx_%s\n", syscall->snake_name().c_str(),
                   syscall->snake_name().c_str());
  }

  writer->Puts("\n");
  for (const auto& syscall : library.syscalls()) {
    writer->Printf("//go:linkname vdso_zx_%s vdso_zx_%s\n", syscall->snake_name().c_str(),
                   syscall->snake_name().c_str());
  }

  writer->Puts("\n");
  for (const auto& syscall : library.syscalls()) {
    writer->Puts("//go:noescape\n");
    writer->Puts("//go:nosplit\n");
    PrintStub(writer, syscall.get());
    writer->Puts("\n");
  }

  writer->Puts("var (\n");
  for (const auto& syscall : library.syscalls()) {
    writer->Printf("\tvdso_zx_%s uintptr\n", syscall->snake_name().c_str());
  }
  writer->Puts(")\n");

  return true;
}

bool GoVdsoArm64Calls(const SyscallLibrary& library, Writer* writer) {
  return VdsoCalls(library, writer, Arch::kArm64);
}

bool GoVdsoX86Calls(const SyscallLibrary& library, Writer* writer) {
  return VdsoCalls(library, writer, Arch::kX86);
}
