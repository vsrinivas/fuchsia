// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/mdns_params.h"

#include <functional>
#include <iostream>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/syslog/cpp/logger.h"

namespace mdns {
namespace {

struct Command {
  std::string name_;
  MdnsParams::CommandVerb verb_;
  uint32_t arg_count_;
  std::function<bool(const std::vector<std::string>& args)> action_;
};

static const std::string kTcpSuffix = "._tcp.";
static const std::string kUdpSuffix = "._udp.";

}  // namespace

// TODO(dalesat): Remove publish/unpublish commands.
MdnsParams::MdnsParams(const fxl::CommandLine& command_line) {
  std::vector<Command> commands{
      {"resolve", CommandVerb::kResolve, 1,
       [this](const std::vector<std::string>& args) {
         return ParseHostName(args[1], &host_name_);
       }},
      {"subscribe", CommandVerb::kSubscribe, 1,
       [this](const std::vector<std::string>& args) {
         return ParseServiceName(args[1], &service_name_);
       }},
      {"respond", CommandVerb::kRespond, 3, [this](const std::vector<std::string>& args) {
         if (!ParseServiceName(args[1], &service_name_) ||
             !ParseInstanceName(args[2], &instance_name_)) {
           return false;
         }

         if (!Parse(args[3], &port_)) {
           std::cout << "'" << args[3] << "' is not a valid port\n\n";
           return false;
         }

         return true;
       }}};

  is_valid_ = false;

  if (command_line.positional_args().empty()) {
    Usage();
    return;
  }

  std::string value_string;

  if (command_line.GetOptionValue("timeout", &value_string) &&
      !Parse(value_string, &timeout_seconds_)) {
    std::cout << "'" << value_string << "' is not a valid timeout value\n\n";
    Usage();
    return;
  }

  if (command_line.GetOptionValue("text", &value_string) && !Parse(value_string, &text_)) {
    std::cout << "'" << value_string << "' is not a valid text value\n\n";
    Usage();
    return;
  }

  if (command_line.GetOptionValue("announce", &value_string) && !Parse(value_string, &announce_)) {
    std::cout << "'" << value_string << "' is not a valid announce value\n\n";
    Usage();
    return;
  }

  for (std::string& subtype : announce_) {
    FX_DCHECK(!subtype.empty());
    if (subtype[subtype.size() - 1] == '.') {
      std::cout << "subtype '" << subtype << "' must not end in '.'\n\n";
      Usage();
      return;
    }
  }

  const std::string& verb = command_line.positional_args()[0];
  for (const Command& command : commands) {
    if (verb == command.name_) {
      if (command_line.positional_args().size() != command.arg_count_ + 1) {
        Usage();
        return;
      }

      if (command.action_ && !command.action_(command_line.positional_args())) {
        Usage();
        return;
      }

      command_verb_ = command.verb_;
      is_valid_ = true;
    }
  }

  if (!is_valid_) {
    Usage();
  }
}

void MdnsParams::Usage() {
  std::cout << "commands:\n";
  std::cout << "    resolve <host_name>\n";
  std::cout << "    subscribe <service_name>\n";
  std::cout << "    respond <service_name> <instance_name> <port>\n";
  std::cout << "options:\n";
  std::cout << "    --timeout=<seconds>       # applies to resolve\n";
  std::cout << "    --text=<text,...>         # applies to respond\n";
  std::cout << "    --announce=<subtype,...>  # applies to respond\n";
  std::cout << "options must precede the command\n";
  std::cout << "<host_name> and <instance_name> cannot end in '.'\n";
  std::cout << "<service_name> must start with '_' and end in '._tcp.' or '._udp.'\n";
}

bool MdnsParams::Parse(const std::string& string_value, uint16_t* out) {
  FX_DCHECK(out);

  std::istringstream istream(string_value);
  return (istream >> *out) && istream.eof();
}

bool MdnsParams::Parse(const std::string& string_value, uint32_t* out) {
  FX_DCHECK(out);

  std::istringstream istream(string_value);
  return (istream >> *out) && istream.eof();
}

bool MdnsParams::Parse(const std::string& string_value, std::vector<std::string>* out) {
  FX_DCHECK(out);

  if (string_value.empty()) {
    return false;
  }

  std::vector<std::string> result =
      fxl::SplitStringCopy(string_value, ",", fxl::kTrimWhitespace, fxl::kSplitWantAll);

  for (std::string& s : result) {
    if (s.empty()) {
      return false;
    }
  }

  *out = result;

  return true;
}

bool MdnsParams::ParseHostName(const std::string& string_value, std::string* out) {
  FX_DCHECK(out);

  if (string_value.empty() || string_value[string_value.size() - 1] == '.') {
    std::cout << "'" << string_value << "' is not a valid host name\n\n";
    return false;
  }

  *out = string_value;
  return true;
}

bool MdnsParams::ParseServiceName(const std::string& string_value, std::string* out) {
  FX_DCHECK(out);

  if (string_value.size() <= kTcpSuffix.size() + 1 || string_value.compare(0, 1, "_") != 0 ||
      (string_value.compare(string_value.size() - kTcpSuffix.size(), kTcpSuffix.size(),
                            kTcpSuffix) != 0 &&
       string_value.compare(string_value.size() - kUdpSuffix.size(), kUdpSuffix.size(),
                            kUdpSuffix) != 0)) {
    std::cout << "'" << string_value << "' is not a valid service name\n\n";
    return false;
  }

  *out = string_value;
  return true;
}

bool MdnsParams::ParseInstanceName(const std::string& string_value, std::string* out) {
  FX_DCHECK(out);

  if (string_value.empty() || string_value[string_value.size() - 1] == '.') {
    std::cout << "'" << string_value << "' is not a instance name\n\n";
    return false;
  }

  *out = string_value;
  return true;
}

}  // namespace mdns
