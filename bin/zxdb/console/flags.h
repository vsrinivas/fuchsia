// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CONSOLE_FLAGS_H_
#define GARNET_BIN_ZXDB_CONSOLE_FLAGS_H_

#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/actions.h"
#include "garnet/bin/zxdb/console/console.h"

namespace zxdb {

// Flag Processing -------------------------------------------------------------

struct FlagRecord;  // Defined below

// Defines what the caller should do with the processing
// Higher priority means that it trumps other kind of processing results when
// evaluating multiple flags
enum class FlagProcessResult : uint32_t {
  kContinue,  // Go to interactive mode
  kActions,   // The flags want to run actions.
              // Schedule them through ActionFlow.
  kQuit,      // Quit the application without running interactive mode.
  // Error trumps everything.
  kError = std::numeric_limits<uint32_t>::max()
};

// Parses the command line and communicates how it wants the caller to react.
// If any error occured Err will have the message.
// The action vector will only be valid if kActions is returned.
FlagProcessResult ProcessCommandLine(const fxl::CommandLine&, Err*,
                                     std::vector<Action>* actions);

// FlagRecord ------------------------------------------------------------------

// Right now FlagRecord is very simple and should be expanded to support more
// interesting cases. Things to be added:
// - Support short form (eg. -s)
// - Multiple options
// - Different values (now everything is a string, but could be enforced that a
//   flag expects ints, doubles, etc.
struct FlagRecord {
  FlagRecord(const char* name, const char* long_form, const char* short_form,
             const char* long_help, const char* short_help,
             const char* argument_name, const char* default_value);

  // The name of the flag displayed on the long description
  const char* name;
  // Double-hyphen form (ie. --(*long_form) )
  const char* long_form;
  // One-hyphen short form (ie. -(*short_form) )
  // nullptr means it's not available
  const char* short_form = nullptr;
  const char* long_help;
  const char* short_help;
  const char* argument_name = nullptr;
  const char* default_value = nullptr;
};

// Flag Helpers ----------------------------------------------------------------

// Constructs all the flags registered in the system. If given a list of
// FlagRecord, it will return it. That is used for testing in order to prvide
// a fake list of flags.
// Defined in flags_impl.cc, as it needs to know about the implemented flags.
std::vector<FlagRecord> InitializeFlags();

// Unique list of all the flags present in the system.
// By default will be initialized with the flags installed by InitializeFlags,
// but can be overriden (for tests) by calling OverrideFlags.
const std::vector<FlagRecord>& GetFlags();

void OverrideFlags(const std::vector<FlagRecord>&);

const FlagRecord* GetFlagFromName(const std::string& name);

const FlagRecord* GetFlagFromOption(const fxl::CommandLine::Option& option);

// Creates the long description to be outputted by --help <FLAG> command
std::string GetFlagLongDescription(const FlagRecord& flag);

// Gets the call signature for this flag
std::string GetFlagSignature(const FlagRecord& flag);

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CONSOLE_FLAGS_H_
