// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/target.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"

namespace zxdb {

// Schema Definition -----------------------------------------------------------

static const char kShowStdoutDescription[] =
    R"(  Whether this process should pipe its stdout/stderr to zxdb.
  If not set for a particular process, it will default to the system-wide
  setting.)";

const char* ClientSettings::Target::kBuildDirs = "build-dirs";
const char* ClientSettings::Target::kBuildDirsDescription =
    R"(  List of paths to build direcories. These are the directories to which paths in
  the symbol files are relative to. When finding a source file, the debugger
  will search for it relative to each of these directories (there can be more
  than one because some files may be compiled in different direcrories than
  others).

  These directories don't necessarily need to exist on the local system. When
  using a crash dump and symbols from another computer you can specify where
  that computer's build directory would have been given your code location so
  relative paths will resolve to the correct local files.)";

const char* ClientSettings::Target::kVectorFormat = "vector-format";
const char* ClientSettings::Target::kVectorFormatDescription =
    R"(  How to treat vector registers.

  This affects the display of vector registers in the "regs" command as well
  as what it means when you type a register name in an expression.

  Possible values:

    i8 / u8     : Array of signed/unsigned 8-bit integers.
    i16 / u16   : Array of signed/unsigned 16-bit integers.
    i32 / u32   : Array of signed/unsigned 32-bit integers.
    i64 / u64   : Array of signed/unsigned 64-bit integers.
    i128 / u128 : Array of signed/unsigned 128-bit integers.
    float       : Array of single-precision floating point.
    double      : Array of double-precision floating point.)";

// static
std::vector<std::string> ClientSettings::Target::GetVectorFormatOptions() {
  return std::vector<std::string>{
      kVectorRegisterFormatStr_Signed8,   kVectorRegisterFormatStr_Unsigned8,
      kVectorRegisterFormatStr_Signed16,  kVectorRegisterFormatStr_Unsigned16,
      kVectorRegisterFormatStr_Signed32,  kVectorRegisterFormatStr_Unsigned32,
      kVectorRegisterFormatStr_Signed64,  kVectorRegisterFormatStr_Unsigned64,
      kVectorRegisterFormatStr_Signed128, kVectorRegisterFormatStr_Unsigned128,
      kVectorRegisterFormatStr_Float,     kVectorRegisterFormatStr_Double};
}

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddBool(ClientSettings::System::kShowStdout, kShowStdoutDescription, true);

  schema->AddList(ClientSettings::Target::kBuildDirs, ClientSettings::Target::kBuildDirsDescription,
                  {});

  schema->AddBool(ClientSettings::Thread::kDebugStepping,
                  ClientSettings::Thread::kDebugSteppingDescription, false);

  schema->AddString(
      ClientSettings::Target::kVectorFormat, ClientSettings::Target::kVectorFormatDescription,
      kVectorRegisterFormatStr_Double, ClientSettings::Target::GetVectorFormatOptions());

  return schema;
}

}  // namespace

// Target Implementation ---------------------------------------------------------------------------

Target::Target(Session* session)
    : ClientObject(session),
      // Implementations can set up fallbacks if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}

Target::~Target() = default;

fxl::WeakPtr<Target> Target::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

fxl::RefPtr<SettingSchema> Target::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
