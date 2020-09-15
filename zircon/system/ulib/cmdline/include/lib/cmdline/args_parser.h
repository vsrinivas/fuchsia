// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CMDLINE_ARGS_PARSER_H_
#define LIB_CMDLINE_ARGS_PARSER_H_

#include <lib/cmdline/optional.h>
#include <lib/cmdline/status.h>

#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cmdline {

// A command line arguments parser.
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
// The command line args parser has two layers. The lower "general" layer is a
// concrete class that parses the command line and calls callbacks for
// switches that have been registered.
//
// On top of this is the ArgsParser which is a template that fills
// a struct with registered options. This is what most code will want, but
// you can still register custom callbacks for more complex behavior.
class GeneralArgsParser {
 public:
  // The callbacks may return an error to indicate a problem with the argument.
  // In this case, parsing will stop and the error will be returned. These
  // callbacks are called in order as switches are processed, so can be
  // called more than once.

  // Callback used for command-line switch presence checks. There is no value
  // and if one is provided it will be an error.
  using OnOffSwitchCallback = std::function<void()>;

  // Callback used for string-value switches.
  using StringCallback = std::function<Status(const std::string&)>;

  GeneralArgsParser();
  ~GeneralArgsParser();

  // disallow copy and assign
  GeneralArgsParser(const GeneralArgsParser&) = delete;
  GeneralArgsParser& operator=(const GeneralArgsParser&) = delete;

  // The parameter type (or whether there is a parameter at all) is controlled
  // by the type of callback passed to the AddGeneralSwitch command.
  //
  // If there is no short name, pass a zero for the character.
  //
  // The callback will be called if the switch is specified. With passed-in
  // strings must outlive this class (they're assumed to be static).
  void AddGeneralSwitch(const char* long_name, const char short_name, const char* help,
                        OnOffSwitchCallback on_switch, OnOffSwitchCallback off_switch = nullptr);
  void AddGeneralSwitch(const char* long_name, const char short_name, const char* help,
                        StringCallback);

  // Constructs a help reference for all switches based on the help strings
  // passed to AddGeneralSwitch().
  std::string GetHelp() const;

  // Parses the given command line. The callbacks are called for any provided
  // switches, and any non-switch values are placed into the given output
  // vector.
  Status ParseGeneral(int argc, const char* const argv[], std::vector<std::string>* params) const;

 private:
  struct Record {
    const char* long_name = nullptr;
    char short_name = 0;
    const char* help_text = nullptr;

    // Only one of these should be non-null, indicates the parameter type.
    OnOffSwitchCallback on_switch_callback;
    StringCallback string_callback;

    // This callback should also be included if allowing --no<switchname>
    OnOffSwitchCallback off_switch_callback;
  };

  // Returns true if this record takes an argument.
  static bool NeedsArg(const Record* record);

  std::vector<Record> records_;
  std::string invalid_option_suggestion_ = "Try --help";
};

namespace internal {
// Split a string into substrings by a delimiter.
std::vector<std::string> SplitString(const std::string& input, char delimiter);
}  // namespace internal

// ArgsParser -----------------------------------------------------------
//
// Parses a command line into a struct and a vector of string parameters.
template <typename ResultStruct>
class ArgsParser : public GeneralArgsParser {
 public:
  ArgsParser() = default;
  ~ArgsParser() = default;

  // disallow copy and assign
  ArgsParser(const ArgsParser&) = delete;
  ArgsParser& operator=(const ArgsParser&) = delete;

  // Presence detector for flags that have no values ("--enable-foo"). It sets
  // a boolean to true if the parameter is present on the command line. The
  // structure should default the boolean to false to detect a set. If a value
  // is present for the switch ("--enable-foo=bar") it will give an error.
  // If the default for the bool is true, the user can disable a boolean
  // switch by prefixing the long name with "no", as in: "--nofoo".
  //
  // Example
  //   struct MyOptions {
  //     bool foo_set = false;
  //   };
  //   ArgsParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo_set);
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 bool ResultStruct::*value) {
    AddGeneralSwitch(
        long_name, short_name, help, [this, value]() { result_.*value = true; },
        [this, value]() { result_.*value = false; });
  }

  // Sets a std::optional with the value if the parameter is present. The
  // value will be required.
  //
  // If the optional validator lambda is given, it will be called with the
  // string value for the parameter, and invalid values can be rejected by
  // returning a cmdline::Status::Error("Your message"); otherwise return
  // cmdline::Status::Ok().
  //
  // Example
  //   struct MyOptions {
  //     std::optional<std::string> foo;
  //   };
  //   ArgsParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  //
  // For optional values of other types, use the cmdline::Optional type, in
  // "lib/cmdline/optional.h".
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 std::optional<std::string> ResultStruct::*value,
                 StringCallback validator = nullptr) {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, value, validator](const std::string& v) -> Status {
                       if (validator != nullptr) {
                         Status status = validator(v);
                         if (status.has_error()) {
                           return status;
                         }
                       }
                       result_.*value = v;
                       return Status::Ok();
                     });
  }

  // Sets a Optional<bool> with the value if the switch is present. The value
  // will be required.
  //
  // Note that std::optional<bool> is not supported because this type can
  // facilitate error-prone uses. std::optional<> implements |operator bool()|
  // to return true if the value is set. The compiler will, thus, allow a
  // std::optional<bool> to be used in a boolean expression, which might appear
  // to resolve to the value, but is not. The boolean expression would
  // evaluate the wrong boolean, creating a bug that is hard to detect
  // when reviewing the code.
  //
  // Example
  //   struct MyOptions {
  //     Optional<bool> foo;
  //   };
  //   ArgsParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 Optional<bool> ResultStruct::*value) {
    AddGeneralSwitch(
        long_name, short_name, help, [this, value]() { result_.*value = true; },
        [this, value]() { result_.*value = false; });
  }

  // Sets a command-line option of any type streamable to an iostream (via
  // operator ">>") with the value, if the parameter is present. The value
  // will be required.
  //
  // If the optional validator lambda is given, it will be called with the
  // string value for the parameter, and invalid values can be rejected by
  // returning a cmdline::Status::Error("Your message"); otherwise return
  // cmdline::Status::Ok().
  //
  // Example
  //   struct MyOptions {
  //     size_t foo;  // Note the type could be int, double, std::string, ...
  //   };
  //   ArgsParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  template <typename T>
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 T ResultStruct::*value, StringCallback validator = nullptr) {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, long_name, value, validator](const std::string& v) -> Status {
                       if (validator != nullptr) {
                         Status status = validator(v);
                         if (status.has_error()) {
                           return status;
                         }
                       }
                       std::stringstream ss(v);
                       ss >> result_.*value;
                       if (ss.fail()) {
                         return Status::Error("'" + v + "' is invalid for --" + long_name);
                       }
                       std::string trailing;
                       ss >> trailing;
                       if (trailing.size() > 0) {
                         return Status::Error("Invalid trailing characters '" + trailing +
                                              "' for --" + long_name);
                       }
                       return Status::Ok();
                     });
  }

  // Collects a list of any type streamable to an iostream (via operator ">>")
  // with the value passed with this flag. This allows multiple flag
  // invocations. For example "-f foo -f bar" would produce a vector
  // { "foo", "bar" }
  //
  // If the optional validator lambda is given, it will be called with the
  // string value for the parameter, and invalid values can be rejected by
  // returning a cmdline::Status::Error("Your message"); otherwise return
  // cmdline::Status::Ok().
  //
  // If the optional delimiters are given, the list can be passed in as a single
  // string split by delimiters. Whitespace will be trimmed from the ends of the values.
  // For example with the delimiter "," the argument "-f foo,bar" would produce a vector
  // { "foo", "bar" }
  //
  // Example
  //   struct MyOptions {
  //     std::vector<std::string> foo;  // Note the type could be int, double, ...
  //   };
  //   ArgsParser<MyOptions> parser;
  //   parser.AddSwitch("foo", 'f', kFooHelp, &MyOptions::foo);
  template <typename T>
  void AddSwitch(const char* long_name, const char short_name, const char* help,
                 std::vector<T> ResultStruct::*value, StringCallback validator = nullptr,
                 char delimiter = '\0') {
    AddGeneralSwitch(long_name, short_name, help,
                     [this, long_name, value, validator = std::move(validator),
                      delimiter](const std::string& input) -> Status {
                       std::vector<std::string> vs = internal::SplitString(input, delimiter);
                       for (const std::string& v : vs) {
                         if (validator != nullptr) {
                           Status status = validator(v);
                           if (status.has_error()) {
                             return status;
                           }
                         }
                         T tmp_val;
                         std::stringstream ss(v);
                         ss >> tmp_val;
                         if (ss.fail()) {
                           return Status::Error("'" + v + "' is invalid for --" + long_name);
                         }
                         std::string trailing;
                         ss >> trailing;
                         if (!trailing.empty()) {
                           return Status::Error("Invalid trailing characters '" + trailing +
                                                "' for --" + long_name);
                         }
                         (result_.*value).push_back(tmp_val);
                       }

                       return Status::Ok();
                     });
  }

  // Parses the given command line, returning the success.
  //
  // Example
  //   MyOptions options;
  //   std::vector<std::string> params;
  //   Status status = parser.Parse(argc, argv, &options, &params);
  //   if (status.has_error())
  //     <print error and return>
  //   <use options and params>
  Status Parse(int argc, const char* const argv[], ResultStruct* options,
               std::vector<std::string>* params) {
    Status status = GeneralArgsParser::ParseGeneral(argc, argv, params);
    if (status.has_error())
      return status;

    *options = std::move(result_);
    return Status::Ok();
  }

 private:
  // Make it harder to accidentally call the base class' parse function since
  // this won't return any options.
  Status ParseGeneral(int argc, const char* const argv[],
                      std::vector<std::string>* params) const = delete;

  // Collects the values while Parse() is running. This needs to be a member
  // because the lambdas registered for the ArgsParser reference into
  // it.
  ResultStruct result_;
};

}  // namespace cmdline

#endif  // LIB_CMDLINE_ARGS_PARSER_H_
