// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>
#include <fstream>

#include "generator.h"

using std::ofstream;

static constexpr char kAuthors[] = "The Fuchsia Authors";

bool Generator::header(ofstream& os) const {
  auto t = std::time(nullptr);
  auto ltime = std::localtime(&t);

  os << "// Copyright " << ltime->tm_year + 1900
     << " " << kAuthors << ". All rights reserved.\n";
  os << "// This is a GENERATED file. The license governing this file can be ";
  os << "found in the LICENSE file.\n\n";

  return os.good();
}

bool Generator::footer(ofstream& os) const {
  os << "\n";
  return os.good();
}

bool X86AssemblyGenerator::syscall(ofstream& os, const Syscall& sc) const {
  if (sc.is_vdso())
    return true;
  // SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) m_syscall nargs64, mx_##name, n
  os << syscall_macro_ << " " << sc.num_kernel_args() << " "
     << name_prefix_ << sc.name << " " << sc.index << "\n";
  return os.good();
}

bool Arm64AssemblyGenerator::syscall(ofstream& os, const Syscall& sc) const {
  if (sc.is_vdso())
    return true;
  // SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) m_syscall mx_##name, n
  os << syscall_macro_ << " " << name_prefix_ << sc.name << " " << sc.index << "\n";
  return os.good();
}

bool SyscallNumbersGenerator::syscall(ofstream& os, const Syscall& sc) const {
  if (sc.is_vdso())
    return true;
  os << define_prefix_ << sc.name << " " << sc.index << "\n";
  return os.good();
}

bool TraceInfoGenerator::syscall(ofstream& os, const Syscall& sc) const {
  if (sc.is_vdso())
    return true;
  // Can be injected as an array of structs or into a tuple-like C++ container.
  os << "{" << sc.index << ", " << sc.num_kernel_args() << ", "
     << '"' << sc.name << "\"},\n";

  return os.good();
}
