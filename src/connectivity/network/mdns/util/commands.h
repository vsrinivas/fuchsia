// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_COMMANDS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_COMMANDS_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/zx/time.h>

#include <string>

#include "src/lib/fxl/command_line.h"
#include "src/lib/inet/ip_address.h"

namespace mdns {

enum class CommandVerb {
  kResolveHost,
  kResolveInstance,
  kSubscribeHost,
  kSubscribeService,
  kPublishHost,
  kPublishInstance,
  kUnsubscribeHost,
  kUnsubscribeService,
  kUnpublishHost,
  kUnpublishInstance,
  kHelp,
  kQuit,

  kMalformed,
  kEmpty,
};

class Command {
 public:
  static const std::string kResolve;
  static const std::string kSubscribe;
  static const std::string kPublish;
  static const std::string kUnsubscribe;
  static const std::string kUnpublish;
  static const std::string kHelp;
  static const std::string kQuit;

  static const std::string kAllServices;

  static void ShowHelp(CommandVerb command_verb);

  CommandVerb verb() const { return verb_; }

  // Positional parameters.
  const std::string& host_name() const { return host_name_; }
  const std::string& service_name() const { return service_name_; }
  const std::string& instance_name() const { return instance_name_; }
  uint16_t port() const { return port_; }
  const std::vector<std::string>& text() const { return text_; }
  const std::vector<inet::IpAddress>& addresses() const { return addresses_; }
  CommandVerb help_verb() const { return help_verb_; }

  // Options.
  zx::duration timeout() const { return timeout_; }
  fuchsia::net::mdns::Media media() const { return media_; }
  fuchsia::net::mdns::IpVersions ip_versions() const { return ip_versions_; }
  bool exclude_local() const { return exclude_local_; }
  bool exclude_local_proxies() const { return exclude_local_proxies_; }
  bool probe() const { return probe_; }
  uint16_t srv_priority() const { return srv_priority_; }
  uint16_t srv_weight() const { return srv_weight_; }
  zx::duration ptr_ttl() const { return ptr_ttl_; }
  zx::duration srv_ttl() const { return srv_ttl_; }
  zx::duration txt_ttl() const { return txt_ttl_; }
  const std::string& proxy_host_name() const { return proxy_host_name_; }

  void ShowHelp() const;

 private:
  explicit Command(CommandVerb verb) : verb_(verb) {}

  static Command ResolveHost(std::string host_name) {
    Command result(CommandVerb::kResolveHost);
    result.host_name_ = std::move(host_name);
    return result;
  }

  static Command ResolveInstance(std::string instance_name, std::string service_name) {
    Command result(CommandVerb::kResolveInstance);
    result.service_name_ = std::move(service_name);
    result.instance_name_ = std::move(instance_name);
    return result;
  }

  static Command SubscribeHost(std::string host_name) {
    Command result(CommandVerb::kSubscribeHost);
    result.host_name_ = std::move(host_name);
    return result;
  }

  static Command SubscribeService(std::string service_name) {
    Command result(CommandVerb::kSubscribeService);
    result.service_name_ = std::move(service_name);
    return result;
  }

  static Command PublishHost(std::string host_name, std::vector<inet::IpAddress> addresses) {
    Command result(CommandVerb::kPublishHost);
    result.host_name_ = std::move(host_name);
    result.addresses_ = std::move(addresses);
    return result;
  }

  static Command PublishInstance(std::string instance_name, std::string service_name, uint16_t port,
                                 std::vector<std::string> text) {
    Command result(CommandVerb::kPublishInstance);
    result.service_name_ = std::move(service_name);
    result.instance_name_ = std::move(instance_name);
    result.port_ = port;
    result.text_ = std::move(text);
    return result;
  }

  static Command UnsubscribeHost(std::string host_name) {
    Command result(CommandVerb::kUnsubscribeHost);
    result.host_name_ = std::move(host_name);
    return result;
  }

  static Command UnsubscribeService(std::string service_name) {
    Command result(CommandVerb::kUnsubscribeService);
    result.service_name_ = std::move(service_name);
    return result;
  }

  static Command UnpublishHost(std::string host_name) {
    Command result(CommandVerb::kUnpublishHost);
    result.host_name_ = std::move(host_name);
    return result;
  }

  static Command UnpublishInstance(std::string instance_name, std::string service_name) {
    Command result(CommandVerb::kUnpublishInstance);
    result.instance_name_ = std::move(instance_name);
    result.service_name_ = std::move(service_name);
    return result;
  }

  static Command Help(CommandVerb help_verb = CommandVerb::kEmpty) {
    Command result(CommandVerb::kHelp);
    result.help_verb_ = help_verb;
    return result;
  }

  static Command Quit() { return Command(CommandVerb::kQuit); }

  static Command Malformed() { return Command(CommandVerb::kMalformed); }

  static Command Empty() { return Command(CommandVerb::kEmpty); }

  CommandVerb verb_;
  std::string host_name_;
  std::string service_name_;
  std::string instance_name_;
  uint16_t port_ = 0;
  std::vector<std::string> text_;
  std::vector<inet::IpAddress> addresses_;
  CommandVerb help_verb_ = CommandVerb::kEmpty;

  zx::duration timeout_ = zx::sec(3);
  fuchsia::net::mdns::Media media_ =
      fuchsia::net::mdns::Media::WIRED | fuchsia::net::mdns::Media::WIRELESS;
  fuchsia::net::mdns::IpVersions ip_versions_ =
      fuchsia::net::mdns::IpVersions::V4 | fuchsia::net::mdns::IpVersions::V6;
  bool exclude_local_ = false;
  bool exclude_local_proxies_ = false;
  bool probe_ = true;
  uint16_t srv_priority_ = 0;
  uint16_t srv_weight_ = 0;
  zx::duration ptr_ttl_ = zx::sec(4500);
  zx::duration srv_ttl_ = zx::sec(120);
  zx::duration txt_ttl_ = zx::sec(4500);
  std::string proxy_host_name_;

  friend class CommandParser;
};

class CommandParser {
 public:
  explicit CommandParser(const std::string& command_line);

  CommandParser(int argc, const char** argv);

  ~CommandParser() = default;

  Command Parse();

 private:
  static constexpr size_t kMaxDnsLabelLength = 63;

  // A MatchXxx method returns true and updates |pos_| if the item is matched. If the item is not
  // matched, the method returns false and leaves |pos_| unchanged. Reference parameters ending
  // in '_out' are used to deliver parsed values to the caller.

  // Matches zero or more whitespace-prefixed options. The set of allows options depends on
  // |command.verb()|, which must be initialized.
  bool MatchOptions(Command& command);

  // Matches a host full name (including the '.local.' suffix) and delivers the host simple name
  // (not including the suffix).
  bool MatchHostName(std::string& value_out);

  // Matches a full instance name and delivers the simple instance name and the service name.
  bool MatchInstanceName(std::string& instance_name_out, std::string& service_name_out);

  // Matches a service name, including the '._tcp' or '._udp.' suffix".
  bool MatchServiceName(std::string& value_out);

  // Matches a |uint16_t|.
  bool MatchUint16(uint16_t& value_out);

  // Matches a list of text strings.
  bool MatchText(std::vector<std::string>& value_out);

  // Matches a text string.
  bool MatchTextString(std::string& value_out);

  // Matches a list of IP addresses.
  bool MatchAddresses(std::vector<inet::IpAddress>& value_out);

  // Matches an IP address terminated by end-of-input, whitespace, or ','.
  bool MatchAddress(inet::IpAddress& value_out);

  // Matches a DNS label containing no whitespace.
  bool MatchDnsLabel(std::string& value_out, size_t max_length = kMaxDnsLabelLength);

  // Matches a positive number of seconds.
  bool MatchSeconds(zx::duration& value_out);

  // Matches one or more media values with comma separators.
  bool MatchMedia(fuchsia::net::mdns::Media& value_out);

  // Matches a media value.
  bool MatchMedium(fuchsia::net::mdns::Media& value_out);

  // Matches one or more IP versions values with comma separators.
  bool MatchIpVersions(fuchsia::net::mdns::IpVersions& value_out);

  // Matches an IP versions value.
  bool MatchIpVersion(fuchsia::net::mdns::IpVersions& value_out);

  // Matches true or false.
  bool MatchBool(bool& value_out);

  // Matches |literal|.
  bool MatchLiteral(const std::string& literal);

  // Matches a decimal digit.
  bool MatchDigit(uint8_t& value_out);

  // Matches one or more whitespace characters.
  bool MatchWhitespace();

  // Matches end-of-input. Does not update |pos_|.
  bool MatchEnd() const { return pos_ == str_.size(); }

  // Matches any amount of whitespace followed by end-of-input.
  bool MatchWhitespaceEnd();

  // Returns the number of characters remaining to be parsed.
  size_t chars_remaining() const { return str_.size() - pos_; }

  // Returns the characters remaining to be parsed.
  const char* remaining_chars() { return &str_[pos_]; }

  void ConsumeChars(size_t chars) { pos_ += chars; }

  void SetCharsRemaining(size_t chars_remaining) {
    FX_CHECK(chars_remaining <= str_.size());
    pos_ = str_.size() - chars_remaining;
  }

  const std::string str_;
  size_t pos_ = 0;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_COMMANDS_H_
