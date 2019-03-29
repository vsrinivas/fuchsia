// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// A command line parser.
//
// Background: The fxl::CommandLine class is designed to be either a global or
// passed around, and code can query it for random switch strings as it feels
// necessary. In the fxl::CommandLine model, the set of command-line switches
// isn't known in advance. This means that it can't generate help or validate
// the switches. It also can't support switches with values that don't use an
// equal sign ("-c foo") because it can't disambiguate whether "foo" is a value
// for the "-c" switch or a standalone parameter. fxl::CommandLine also
// supports modification and generation of new command lines.
//
// In contrast, this command-line parser requires registration of all switches
// in advance. It can then validate inputs, generate help, and accept more
// flexible value formatting. This class is designed exclusively as a parser
// which makes it simpler.
//
// The command line parser has two layers. The lower "general" layer is a
// concrete class that parses the command line and calls callbacks for
// switches that have been registered.
//
// On top of this is the CommandLineParser which is a template that fills
// a struct with registered options. This is what most code will want, but
// you can still register custom callbacks for more complex behavior.
class GeneralCommandLineParser {
 public:
  // The callbacks may return an error to indicate a problem with the argument.
  // In this case, parsing will stop and the error will be returned. These
  // callbacks are called in order as switches are processed, so can be
  // called more than once.

  // Callback used for command-line switch presence checks. There is no value
  // and if one is provided it will be an error.
  using NoArgCallback = std::function<void()>;

  // Callback used for string-value switches.
  using StringCallback = std::function<Err(const std::string&)>;

  GeneralCommandLineParser();
  ~GeneralCommandLineParser();

  // The parameter type (or whether there is a parameter at all) is controlled
  // by the type of callback passed to the AddGeneralSwitch command.
  //
  // If there is no short name, pass a zero for the character.
  //
  // The callback will be called if the switch is specified. With passed-in
  // strings must outlive this class (they're assumed to be static).
  void AddGeneralSwitch(const char* long_name, const char short_name,
                        const char* help, NoArgCallback);
  void AddGeneralSwitch(const char* long_name, const char short_name,
                        const char* help, StringCallback);

  // Constructs a help reference for all switches based on the help strings
  // passed to AddGeneralSwitch().
  std::string GetHelp() const;

  // Parses the given command line. The callbacks are called for any provided
  // switches, and any non-switch values are placed into the given output
  // vector.
  Err ParseGeneral(int argc, const char* argv[],
                   std::vector<std::string>* params) const;

 private:
  struct Record {
    const char* long_name = nullptr;
    char short_name = 0;
    const char* help_text = nullptr;

    // Only one of these should be non-null, indicates the parameter type.
    NoArgCallback no_arg_callback;
    StringCallback string_callback;
  };

  // Returns true if this record takes an argument.
  static bool NeedsArg(const Record* record);

  std::vector<Record> records_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GeneralCommandLineParser);
};

// CommandLineParser -----------------------------------------------------------
//
// Parses a command line into a struct and a vector of string parameters.
template <typename ResultStruct>
class CommandLineParser : public GeneralCommandLineParser {
 public:
  CommandLineParser() = default;
  ~CommandLineParser() = default;

  // Presence detector for flags that have no values ("--enable-foo"). It sets
  // a boolean to true if the parameter is present on the command line. The
  // structure should default the boolean to false to detect a set. If a value
  // is present for the switch ("--enable-foo=bar") it will give an error.
  //
  // Example
  //   struct MyOptions {
  //     bool foo_set = false;
  //   };
  //   CommandLineParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo_set);
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 bool ResultStruct::*value) {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, value]() { result_.*value = true; });
  }

  // Sets a std::optional with the value if the parameter is present. The value
  // will be required.
  //
  // Example
  //   struct MyOptions {
  //     std::optional<std::string> foo;
  //   };
  //   CommandLineParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 std::optional<std::string> ResultStruct::*value) {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, value](const std::string& v) -> Err {
                       result_.*value = v;
                       return Err();
                     });
  }

  // Collects a list of all values passed with this flag. This allows multiple
  // flag invocations. For examples "-f foo -f bar" would produce a vector
  // { "foo", "bar" }
  //
  // Example
  //   struct MyOptions {
  //     std::vector<std::string> foo;
  //   };
  //   CommandLineParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 std::vector<std::string> ResultStruct::*value) {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, value](const std::string& v) -> Err {
                       (result_.*value).push_back(v);
                       return Err();
                     });
  }

  // Parses the given command line, returning the success.
  //
  // Example
  //   MyOptions options;
  //   std::vector<std::string> params;
  //   Err err = parser.Parse(argc, argv, &options, &params);
  //   if (err.has_error())
  //     <print error and return>
  //   <use options and params>
  Err Parse(int argc, const char* argv[], ResultStruct* options,
            std::vector<std::string>* params) {
    Err err = GeneralCommandLineParser::ParseGeneral(argc, argv, params);
    if (err.has_error())
      return err;

    *options = std::move(result_);
    return Err();
  }

 private:
  // Make it harder to accidentally call the base class' parse function since
  // this won't return any options.
  Err ParseGeneral(int argc, const char* argv[],
                   std::vector<std::string>* params) const = delete;

  // Collects the values while Parse() is running. This needs to be a member
  // because the lambdas registered for the CommandLineParser reference into
  // it.
  ResultStruct result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandLineParser);
};

}  // namespace zxdb
