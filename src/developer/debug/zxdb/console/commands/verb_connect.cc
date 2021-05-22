// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_connect.h"

#include <map>
#include <string>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/inet_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kUnixSwitch = 1;

const char kConnectShortHelp[] = R"(connect: Connect to a remote system for debugging.)";
const char kConnectHelp[] =
    R"(connect [ <remote_address> ]

  Connects to a debug_agent at the given address/port. With no arguments,
  attempts to reconnect to the previously used remote address.

  See also "disconnect".

Addresses

  Addresses can be of the form "<host> <port>" or "<host>:<port>". When using
  the latter form, IPv6 addresses must be [bracketed]. Otherwise the brackets
  are optional.

Options

  --unix-socket
  -u
      Attempt to connect to a unix socket. In this case <host> is a filesystem path.

Examples

  connect mystem.localnetwork 1234
  connect mystem.localnetwork:1234
  connect 192.168.0.4:1234
  connect 192.168.0.4 1234
  connect [1234:5678::9abc] 1234
  connect 1234:5678::9abc 1234
  connect [1234:5678::9abc]:1234
  connect -u /path/to/socket
)";

// Displays the failed connection error message. Connections are normally initiated on startup
// and it can be difficult to see the message with all the other normal startup messages. This
// can confuse users who wonder why nothing is working. As a result, make the message really big.
void DisplayConnectionFailed(const Err& err) {
  OutputBuffer out;
  out.Append(Syntax::kError, "╒═══════════════════════════════════════════╕\n│ ");
  out.Append(Syntax::kHeading, "Connection to the debugged system failed. ");
  out.Append(Syntax::kError, "│\n╘═══════════════════════════════════════════╛\n");
  out.Append(err);
  out.Append(Syntax::kError, "\n\nThe debugger will not be usable without connecting.\n\n");

  Console::get()->Output(out);
}

Err RunVerbConnect(ConsoleContext* context, const Command& cmd,
                   CommandCallback callback = nullptr) {
  SessionConnectionInfo connection_info;

  if (cmd.HasSwitch(kUnixSwitch)) {
    connection_info.type = SessionConnectionType::kUnix;
    if (cmd.args().size() == 1) {
      connection_info.host = cmd.args()[0];
    } else {
      return Err(ErrType::kInput, "Too many arguments.");
    }
  } else {
    // 0 args means pass empty string and 0 port to try to reconnect.
    if (cmd.args().size() == 1) {
      const std::string& host_port = cmd.args()[0];
      // Provide an additional assist to users if they forget to wrap an IPv6 address in [].
      if (Ipv6HostPortIsMissingBrackets(host_port)) {
        return Err(ErrType::kInput,
                   "For IPv6 addresses use either: \"[::1]:1234\"\n"
                   "or the two-parameter form: \"::1 1234.");
      }
      Err err = ParseHostPort(host_port, &connection_info.host, &connection_info.port);
      if (err.has_error())
        return err;
      connection_info.type = SessionConnectionType::kNetwork;
    } else if (cmd.args().size() == 2) {
      Err err =
          ParseHostPort(cmd.args()[0], cmd.args()[1], &connection_info.host, &connection_info.port);
      if (err.has_error())
        return err;
      connection_info.type = SessionConnectionType::kNetwork;
    } else if (cmd.args().size() > 2) {
      return Err(ErrType::kInput, "Too many arguments.");
    }
  }

  context->session()->Connect(
      connection_info, [callback = std::move(callback), cmd](const Err& err) mutable {
        if (err.has_error()) {
          // Don't display error message if they canceled the connection.
          if (err.type() != ErrType::kCanceled)
            DisplayConnectionFailed(err);
        } else {
          OutputBuffer msg;
          msg.Append("Connected successfully.\n");

          // Assume if there's a callback this is not being run interactively. Otherwise, show the
          // usage tip.
          if (!callback) {
            msg.Append(Syntax::kWarning, "👉 ");
            msg.Append(Syntax::kComment,
                       "Normally you will \"run <program path>\" or \"attach "
                       "<process koid>\".");
          }
          Console::get()->Output(msg);
        }

        if (callback)
          callback(err);
      });
  Console::get()->Output("Connecting (use \"disconnect\" to cancel)...\n");

  return Err();
}

}  // namespace

VerbRecord GetConnectVerbRecord() {
  SwitchRecord unix_switch(kUnixSwitch, false, "unix-socket", 'u');
  VerbRecord connect_record = VerbRecord(&RunVerbConnect, {"connect"}, kConnectShortHelp,
                                         kConnectHelp, CommandGroup::kGeneral);
  connect_record.switches.push_back(unix_switch);
  return connect_record;
}

}  // namespace zxdb
