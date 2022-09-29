// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/commands.h"

#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <iostream>

#include "src/lib/fxl/strings/split_string.h"

namespace mdns {
namespace {

// Concatenates argv[1]..argv[argc - 1] with space separators.
std::string Concatenate(int argc, const char** argv) {
  if (argc == 1) {
    return "";
  }

  std::string result;
  size_t result_size = 0;

  for (int i = 1; i < argc; ++i) {
    result_size += strlen(argv[i]) + 1;
  }

  result_size -= 1;

  result.reserve(result_size);

  result.append(argv[1]);
  for (int i = 2; i < argc; ++i) {
    result.append(" ");
    result.append(argv[i]);
  }

  return result;
}

const std::string kTimeoutOption = "timeout";
const std::string kMediaOption = "media";
const std::string kIpVersionsOption = "ip-versions";
const std::string kExcludeLocalOption = "exclude-local";
const std::string kExcludeLocalProxyOption = "exclude-local-proxies";
const std::string kProbeOption = "probe";
const std::string kSrvPriorityOption = "srv-priority";
const std::string kSrvWeightOption = "srv-weight";
const std::string kPtrTtlOption = "ptr-ttl";
const std::string kSrvTtlOption = "srv-ttl";
const std::string kTxtTtlOption = "txt-ttl";
const std::string kProxyHostOption = "proxy-host";

const std::string kBoolValueTrue = "true";
const std::string kBoolValueFalse = "false";
const std::string kMediaValueWired = "wired";
const std::string kMediaValueWireless = "wireless";
const std::string kIpVersionsValueV4 = "4";
const std::string kIpVersionsValueV6 = "6";

const std::string kLabelSeparator = ".";
const std::string kValueSeparator = ",";
const std::string kOptionPrefix = "--";
const std::string kOptionSeparator = "=";
const std::string kHostSuffix = "local.";
const std::string kTcpSuffix = "._tcp.";
const std::string kUdpSuffix = "._udp.";

constexpr size_t kMaxServiceNameLabelLength = 16;
constexpr size_t kMaxHostNameLength = 253 - 6;  // 6 for local domain.

}  // namespace

const std::string Command::kResolve = "resolve";
const std::string Command::kSubscribe = "subscribe";
const std::string Command::kPublish = "publish";
const std::string Command::kUnsubscribe = "unsubscribe";
const std::string Command::kUnpublish = "unpublish";
const std::string Command::kHelp = "help";
const std::string Command::kQuit = "quit";

const std::string Command::kAllServices = "all";

CommandParser::CommandParser(const std::string& command_line) : str_(command_line) {}

CommandParser::CommandParser(int argc, const char** argv)
    : CommandParser(Concatenate(argc, argv)) {}

Command CommandParser::Parse() {
  MatchWhitespace();

  if (MatchEnd()) {
    // Whitespace only.
    return Command::Empty();
  } else if (MatchLiteral(Command::kResolve)) {
    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    std::string host_name;
    std::string service_name;
    std::string instance_name;
    if (MatchHostName(host_name)) {
      auto command = Command::ResolveHost(host_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kResolveHost);
      }
    } else if (MatchInstanceName(instance_name, service_name)) {
      auto command = Command::ResolveInstance(instance_name, service_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kResolveInstance);
      }
    } else {
      return Command::Help(CommandVerb::kResolveHost);
    }
  } else if (MatchLiteral(Command::kSubscribe)) {
    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    std::string host_name;
    std::string service_name;
    if (MatchHostName(host_name)) {
      auto command = Command::SubscribeHost(host_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kSubscribeHost);
      }
    } else if (MatchServiceName(service_name)) {
      auto command = Command::SubscribeService(service_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kSubscribeService);
      }
    } else if (MatchLiteral(Command::kAllServices)) {
      auto command = Command::SubscribeService(Command::kAllServices);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kSubscribeService);
      }
    } else {
      return Command::Help(CommandVerb::kSubscribeHost);
    }
  } else if (MatchLiteral(Command::kPublish)) {
    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    std::string host_name;
    std::string service_name;
    std::string instance_name;
    if (MatchHostName(host_name)) {
      std::vector<inet::IpAddress> addresses;
      if (!MatchWhitespace() || !(MatchAddresses(addresses))) {
        return Command::Help(CommandVerb::kPublishHost);
      }

      auto command = Command::PublishHost(host_name, addresses);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kPublishHost);
      }
    } else if (MatchInstanceName(instance_name, service_name)) {
      uint16_t port;
      std::vector<std::string> text;

      if (!MatchWhitespace() || !(MatchUint16(port)) || !MatchWhitespace() || !(MatchText(text))) {
        return Command::Help(CommandVerb::kPublishInstance);
      }

      auto command = Command::PublishInstance(instance_name, service_name, port, text);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kPublishInstance);
      }
    } else {
      return Command::Help(CommandVerb::kUnpublishHost);
    }
  } else if (MatchLiteral(Command::kUnsubscribe)) {
    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    std::string host_name;
    std::string service_name;
    if (MatchHostName(host_name)) {
      auto command = Command::UnsubscribeHost(host_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kUnsubscribeHost);
      }
    } else if (MatchServiceName(service_name)) {
      auto command = Command::UnsubscribeService(service_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kUnsubscribeService);
      }
    } else if (MatchLiteral(Command::kAllServices)) {
      auto command = Command::UnsubscribeService(Command::kAllServices);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kSubscribeService);
      }
    } else {
      return Command::Help(CommandVerb::kUnsubscribeHost);
    }
  } else if (MatchLiteral(Command::kUnpublish)) {
    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    std::string host_name;
    std::string service_name;
    std::string instance_name;
    if (MatchHostName(host_name)) {
      auto command = Command::UnpublishHost(host_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kUnpublishHost);
      }
    } else if (MatchInstanceName(instance_name, service_name)) {
      auto command = Command::UnpublishInstance(instance_name, service_name);
      if (MatchOptions(command)) {
        return command;
      } else {
        return Command::Help(CommandVerb::kUnpublishHost);
      }
    } else {
      return Command::Help(CommandVerb::kUnpublishHost);
    }
  } else if (MatchLiteral(Command::kHelp)) {
    if (MatchWhitespaceEnd()) {
      return Command::Help();
    }

    if (!MatchWhitespace()) {
      return Command::Malformed();
    }

    // Here we are using e.g. |CommandVerb::kResolveHost| to specify help for both the 'resolve'
    // commands (both kResolveHost or kResolveInstance).
    if (MatchLiteral(Command::kResolve)) {
      return MatchWhitespaceEnd() ? Command::Help(CommandVerb::kResolveHost) : Command::Malformed();
    } else if (MatchLiteral(Command::kSubscribe)) {
      return MatchWhitespaceEnd() ? Command::Help(CommandVerb::kSubscribeHost)
                                  : Command::Malformed();
    } else if (MatchLiteral(Command::kPublish)) {
      return MatchWhitespaceEnd() ? Command::Help(CommandVerb::kPublishHost) : Command::Malformed();
    } else if (MatchLiteral(Command::kUnsubscribe)) {
      return MatchWhitespaceEnd() ? Command::Help(CommandVerb::kUnsubscribeHost)
                                  : Command::Malformed();
      return Command::Help(CommandVerb::kUnsubscribeHost);
    } else if (MatchLiteral(Command::kUnpublish)) {
      return MatchWhitespaceEnd() ? Command::Help(CommandVerb::kUnpublishHost)
                                  : Command::Malformed();
    } else {
      return Command::Malformed();
    }
  } else if (MatchLiteral(Command::kQuit)) {
    if (MatchWhitespaceEnd()) {
      return Command::Quit();
    } else {
      return Command::Malformed();
    }
  } else {
    return Command::Malformed();
  }
}

bool CommandParser::MatchOptions(Command& command) {
  size_t saved_chars_remaining = chars_remaining();

  while (true) {
    if (MatchWhitespaceEnd()) {
      return true;
    }

    if (!MatchWhitespace()) {
      break;
    }

    if (!MatchLiteral(kOptionPrefix)) {
      break;
    }

    if (MatchLiteral(kTimeoutOption)) {
      switch (command.verb()) {
        case CommandVerb::kResolveHost:
        case CommandVerb::kResolveInstance:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchSeconds(command.timeout_)) {
        break;
      }
    } else if (MatchLiteral(kMediaOption)) {
      switch (command.verb()) {
        case CommandVerb::kResolveHost:
        case CommandVerb::kResolveInstance:
        case CommandVerb::kSubscribeHost:
        case CommandVerb::kSubscribeService:
        case CommandVerb::kPublishHost:
        case CommandVerb::kPublishInstance:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchMedia(command.media_)) {
        break;
      }
    } else if (MatchLiteral(kIpVersionsOption)) {
      switch (command.verb()) {
        case CommandVerb::kResolveHost:
        case CommandVerb::kResolveInstance:
        case CommandVerb::kSubscribeHost:
        case CommandVerb::kSubscribeService:
        case CommandVerb::kPublishHost:
        case CommandVerb::kPublishInstance:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchIpVersions(command.ip_versions_)) {
        break;
      }
    } else if (MatchLiteral(kExcludeLocalOption)) {
      switch (command.verb()) {
        case CommandVerb::kResolveHost:
        case CommandVerb::kResolveInstance:
        case CommandVerb::kSubscribeHost:
        case CommandVerb::kSubscribeService:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchBool(command.exclude_local_)) {
        break;
      }
    } else if (MatchLiteral(kExcludeLocalProxyOption)) {
      switch (command.verb()) {
        case CommandVerb::kResolveHost:
        case CommandVerb::kResolveInstance:
        case CommandVerb::kSubscribeHost:
        case CommandVerb::kSubscribeService:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchBool(command.exclude_local_proxies_)) {
        break;
      }
    } else if (MatchLiteral(kProbeOption)) {
      switch (command.verb()) {
        case CommandVerb::kPublishHost:
        case CommandVerb::kPublishInstance:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchBool(command.probe_)) {
        break;
      }
    } else if (MatchLiteral(kSrvPriorityOption)) {
      if (command.verb() != CommandVerb::kPublishInstance) {
        break;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchUint16(command.srv_priority_)) {
        break;
      }
    } else if (MatchLiteral(kSrvWeightOption)) {
      if (command.verb() != CommandVerb::kPublishInstance) {
        break;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchUint16(command.srv_weight_)) {
        break;
      }
    } else if (MatchLiteral(kPtrTtlOption)) {
      if (command.verb() != CommandVerb::kPublishInstance) {
        break;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchSeconds(command.ptr_ttl_)) {
        break;
      }
    } else if (MatchLiteral(kSrvTtlOption)) {
      if (command.verb() != CommandVerb::kPublishInstance) {
        break;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchSeconds(command.srv_ttl_)) {
        break;
      }
    } else if (MatchLiteral(kTxtTtlOption)) {
      if (command.verb() != CommandVerb::kPublishInstance) {
        break;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchSeconds(command.txt_ttl_)) {
        break;
      }
    } else if (MatchLiteral(kProxyHostOption)) {
      switch (command.verb()) {
        case CommandVerb::kPublishInstance:
        case CommandVerb::kUnpublishInstance:
          break;
        default:
          SetCharsRemaining(saved_chars_remaining);
          return false;
      }

      if (!MatchLiteral(kOptionSeparator) || !MatchHostName(command.proxy_host_name_)) {
        break;
      }
    }
  }

  SetCharsRemaining(saved_chars_remaining);
  return false;
}

bool CommandParser::MatchHostName(std::string& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  std::string label;
  if (!MatchDnsLabel(label)) {
    return false;
  }

  std::string value = std::move(label);

  while (!MatchEnd() && value.size() <= kMaxHostNameLength) {
    if (!MatchLiteral(kLabelSeparator)) {
      break;
    }

    if (MatchLiteral(kHostSuffix)) {
      value_out = value;
      return true;
    }

    value.append(kLabelSeparator);

    if (!MatchDnsLabel(label)) {
      break;
    }

    value.append(label);
  }

  SetCharsRemaining(saved_chars_remaining);
  return false;
}

bool CommandParser::MatchInstanceName(std::string& instance_name_out,
                                      std::string& service_name_out) {
  size_t saved_chars_remaining = chars_remaining();

  std::string instance_name;
  if (MatchDnsLabel(instance_name) && MatchLiteral(kLabelSeparator) &&
      MatchServiceName(service_name_out)) {
    instance_name_out = std::move(instance_name);
    return true;
  }

  SetCharsRemaining(saved_chars_remaining);
  return false;
}

bool CommandParser::MatchServiceName(std::string& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  if (!MatchLiteral("_")) {
    return false;
  }

  std::string label;
  if (MatchDnsLabel(label, kMaxServiceNameLabelLength - 1)) {
    if (MatchLiteral(kTcpSuffix)) {
      value_out = "_";
      value_out.append(label);
      value_out.append(kTcpSuffix);
      return true;
    } else if (MatchLiteral(kUdpSuffix)) {
      value_out = "_";
      value_out.append(label);
      value_out.append(kUdpSuffix);
      return true;
    }
  }

  SetCharsRemaining(saved_chars_remaining);
  return false;
}

bool CommandParser::MatchUint16(uint16_t& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  uint8_t digit;
  if (!MatchDigit(digit)) {
    return false;
  }

  uint16_t value = digit;
  while (MatchDigit(digit)) {
    if (value > (std::numeric_limits<uint16_t>::max() - digit) / 10) {
      // Too large.
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    value = value * 10 + digit;
  }

  value_out = value;
  return true;
}

bool CommandParser::MatchText(std::vector<std::string>& value_out) {
  std::vector<std::string> value;

  while (true) {
    std::string text_string;
    if (!MatchTextString(text_string)) {
      break;
    }

    value.push_back(text_string);
    MatchWhitespace();
  }

  value_out = std::move(value);
  return true;
}

bool CommandParser::MatchTextString(std::string& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  std::string terminator = "\"";
  if (!MatchLiteral(terminator) && (terminator = "'", !MatchLiteral(terminator))) {
    return false;
  }

  auto start = remaining_chars();
  while (!MatchLiteral(terminator)) {
    if (MatchEnd()) {
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    ConsumeChars(1);
  }

  value_out = std::string(start, (remaining_chars() - start) - 1);
  return true;
}

bool CommandParser::MatchAddresses(std::vector<inet::IpAddress>& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  inet::IpAddress address;
  if (!MatchAddress(address)) {
    return false;
  }

  std::vector<inet::IpAddress> value;
  value.push_back(address);

  while (MatchLiteral(",")) {
    if (!MatchAddress(address)) {
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    value.push_back(address);
  }

  value_out = std::move(value);
  return true;
}

bool CommandParser::MatchAddress(inet::IpAddress& value_out) {
  auto pair =
      inet::IpAddress::FromStringView(std::string_view(remaining_chars(), chars_remaining()));
  if (!pair.first.is_valid()) {
    return false;
  }

  value_out = pair.first;
  ConsumeChars(pair.second);
  return true;
}

bool CommandParser::MatchDnsLabel(std::string& value_out, size_t max_length) {
  size_t saved_chars_remaining = chars_remaining();

  std::string value;

  while (!MatchEnd() && *remaining_chars() != '.' && !isspace(*remaining_chars())) {
    if (value.size() == max_length) {
      // Too long.
      value.clear();
      break;
    }

    value.push_back(*remaining_chars());
    ConsumeChars(1);
  }

  if (value.empty()) {
    SetCharsRemaining(saved_chars_remaining);
    return false;
  }

  value_out = value;
  return true;
}

bool CommandParser::MatchSeconds(zx::duration& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  uint8_t digit;
  if (!MatchDigit(digit)) {
    return false;
  }

  uint32_t value = digit;
  while (MatchDigit(digit)) {
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) {
      // Too large.
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    value = value * 10 + digit;
  }

  value_out = zx::sec(value);
  return true;
}

bool CommandParser::MatchMedia(fuchsia::net::mdns::Media& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  fuchsia::net::mdns::Media value;
  if (!MatchMedium(value)) {
    return false;
  }

  while (MatchLiteral(kValueSeparator)) {
    fuchsia::net::mdns::Media v;
    if (!MatchMedium(v)) {
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    value |= v;
  }

  value_out = value;
  return true;
}

bool CommandParser::MatchMedium(fuchsia::net::mdns::Media& value_out) {
  if (MatchLiteral(kMediaValueWired)) {
    value_out = fuchsia::net::mdns::Media::WIRED;
    return true;
  }

  if (MatchLiteral(kMediaValueWireless)) {
    value_out = fuchsia::net::mdns::Media::WIRELESS;
    return true;
  }

  return false;
}

bool CommandParser::MatchIpVersions(fuchsia::net::mdns::IpVersions& value_out) {
  size_t saved_chars_remaining = chars_remaining();

  fuchsia::net::mdns::IpVersions value;
  if (!MatchIpVersion(value)) {
    return false;
  }

  while (MatchLiteral(kValueSeparator)) {
    fuchsia::net::mdns::IpVersions v;
    if (!MatchIpVersion(v)) {
      SetCharsRemaining(saved_chars_remaining);
      return false;
    }

    value |= v;
  }

  value_out = value;
  return true;
}

bool CommandParser::MatchIpVersion(fuchsia::net::mdns::IpVersions& value_out) {
  if (MatchLiteral(kIpVersionsValueV4)) {
    value_out = fuchsia::net::mdns::IpVersions::V4;
    return true;
  }

  if (MatchLiteral(kIpVersionsValueV6)) {
    value_out = fuchsia::net::mdns::IpVersions::V6;
    return true;
  }

  return false;
}

bool CommandParser::MatchBool(bool& value_out) {
  if (MatchLiteral(kBoolValueTrue)) {
    value_out = true;
    return true;
  }

  if (MatchLiteral(kBoolValueFalse)) {
    value_out = false;
    return true;
  }

  return false;
}

bool CommandParser::MatchLiteral(const std::string& literal) {
  if (chars_remaining() < literal.size()) {
    return false;
  }

  if (strncmp(literal.data(), remaining_chars(), literal.size()) != 0) {
    return false;
  }

  ConsumeChars(literal.size());
  return true;
}

bool CommandParser::MatchDigit(uint8_t& value_out) {
  if (MatchEnd() || !isdigit(*remaining_chars())) {
    return false;
  }

  value_out = *remaining_chars() - '0';
  return true;
}

bool CommandParser::MatchWhitespace() {
  if (MatchEnd()) {
    return false;
  }

  if (!isspace(*remaining_chars())) {
    return false;
  }

  for (ConsumeChars(1); !MatchEnd() && isspace(*remaining_chars()); ConsumeChars(1)) {
  }

  return true;
}

bool CommandParser::MatchWhitespaceEnd() {
  size_t saved_chars_remaining = chars_remaining();

  MatchWhitespace();

  if (MatchEnd()) {
    return true;
  }

  SetCharsRemaining(saved_chars_remaining);
  return false;
}

// static
void Command::ShowHelp(CommandVerb command_verb) {
  switch (command_verb) {
    case CommandVerb::kResolveHost:
    case CommandVerb::kResolveInstance:
      std::cout << "resolve <host name>\n";
      std::cout << "    resolves a host\n";
      std::cout << "    <host name>: name of host including `.local.` suffix\n";
      std::cout << "    --timeout=<seconds>                   default: '3'\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --exclude-local=<true|false>          default: `false`\n";
      std::cout << "    --exclude-local-proxies=<true|false>  default: `false`\n";
      std::cout << "\n";
      std::cout << "resolve <instance name>\n";
      std::cout << "    resolves a service instance\n";
      std::cout << "    <instance name>: instance.service including `.tcp_.` or `.udp_.` suffix\n";
      std::cout << "    --timeout=<seconds>                   default: '3'\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --exclude-local=<true|false>          default: `false`\n";
      std::cout << "    --exclude-local-proxies=<true|false>  default: `false`\n";
      break;
    case CommandVerb::kSubscribeHost:
    case CommandVerb::kSubscribeService:
      std::cout << "subscribe <host name>\n";
      std::cout << "    subscribes to a host\n";
      std::cout << "    <host name>: name of host including `.local.` suffix\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --exclude-local=<true|false>          default: `false`\n";
      std::cout << "    --exclude-local-proxies=<true|false>  default: `false`\n";
      std::cout << "\n";
      std::cout << "subscribe <service name>\n";
      std::cout << "    subscribes to a service\n";
      std::cout << "    <service name>: name of service including `.tcp_.` or `.udp_.` suffix\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --exclude-local=<true|false>          default: `false`\n";
      std::cout << "    --exclude-local-proxies=<true|false>  default: `false`\n";
      std::cout << "\n";
      std::cout << "subscribe all\n";
      std::cout << "    subscribes to all services\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --exclude-local=<true|false>          default: `false`\n";
      std::cout << "    --exclude-local-proxies=<true|false>  default: `false`\n";
      break;
    case CommandVerb::kPublishHost:
    case CommandVerb::kPublishInstance:
      std::cout << "publish <host name> <addresses>\n";
      std::cout << "    publishes a proxy host\n";
      std::cout << "    <host name>: name of proxy host including `.local.` suffix\n";
      std::cout << "    <addresses>: IP addresses of proxy host\n";
      std::cout << "    --probe=<true|false>                  default: `true`\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "\n";
      std::cout << "publish <instance name> <port> <text>*\n";
      std::cout << "    publishes to a service instance\n";
      std::cout << "    <instance name>: instance.service including `.tcp_.` or `.udp_.` suffix\n";
      std::cout << "    <port>: port number (decimal)\n";
      std::cout << "    <text>: text string (in single or double quotes)\n";
      std::cout << "    --probe=<true|false>                  default: `true`\n";
      std::cout << "    --media=<media>                       default: `wired,wireless`\n";
      std::cout << "    --ip-versions=<versions>              default: `4,6`\n";
      std::cout << "    --srv-priority=<uint16>               default: `0`\n";
      std::cout << "    --srv-weight=<uint16>                 default: `0`\n";
      std::cout << "    --ptr-ttl=<seconds>                   default: `4500`\n";
      std::cout << "    --srv-ttl=<seconds>                   default: `120`\n";
      std::cout << "    --txt-ttl=<seconds>                   default: `4500`\n";
      std::cout << "    --proxy-host=<host name>              default: none (local)\n";
      break;
    case CommandVerb::kUnsubscribeHost:
    case CommandVerb::kUnsubscribeService:
      std::cout << "unsubscribe <host name>\n";
      std::cout << "    cancels 'subscribe <host name>'\n";
      std::cout << "    <host name>: name of host including `.local.` suffix\n";
      std::cout << "\n";
      std::cout << "unsubscribe <service name>\n";
      std::cout << "    cancels 'subscribe <service name>'\n";
      std::cout << "    <service name>: name of service including `.tcp_.` or `.udp_.` suffix\n";
      std::cout << "\n";
      std::cout << "unsubscribe all\n";
      std::cout << "    cancels 'subscribe all'\n";
      break;
    case CommandVerb::kUnpublishHost:
    case CommandVerb::kUnpublishInstance:
      std::cout << "unpublish <host name>\n";
      std::cout << "    cancels `publish <host name> ...`\n";
      std::cout << "    <host name>: name of proxy host including `.local.` suffix\n";
      std::cout << "\n";
      std::cout << "unpublish <instance name>\n";
      std::cout << "    cancels `publish <instance name> ...`\n";
      std::cout << "    <instance name>: instance.service including `.tcp_.` or `.udp_.` suffix\n";
      std::cout << "    --proxy-host=<host name>              default: none (local)\n";
      break;
    case CommandVerb::kHelp:
    case CommandVerb::kQuit:
    case CommandVerb::kEmpty:
    case CommandVerb::kMalformed:
      std::cout << "commands:\n";
      std::cout << "    resolve <host name> | <instance name>\n";
      std::cout << "    subscribe <host name> | <service name>\n";
      std::cout << "    publish ( <host name> <addresses>) | ( <instance name> <port> <text>) \n";
      std::cout << "    unsubscribe <host name> | <service_name>\n";
      std::cout << "    unpublish <host name> | <instance name>\n";
      std::cout << "    quit\n";
      std::cout << "    help\n";
      std::cout << "    help <command>\n";
      break;
  }
}

void Command::ShowHelp() const { ShowHelp(help_verb()); }

}  // namespace mdns
