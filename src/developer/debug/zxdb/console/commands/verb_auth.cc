// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_auth.h"

#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kAuthShortHelp[] = "auth: Authenticate with a symbol server.";
const char kAuthHelp[] =
    R"(auth [credentials]

  Authenticates with a symbol server. What that meas will depend on the type of
  authentication the sever supports. Run with no arguments to receive
  instructions on how to proceed.

  Must have a valid symbol server noun. See help for sym-server.

Example

  auth my_secret
  sym-server 3 auth some_credential
)";

void RunVerbAuth(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (cmd.args().size() > 1u) {
    return cmd_context->ReportError(Err("auth expects exactly one argument."));
  }

  if (!cmd.sym_server())
    return cmd_context->ReportError(Err("No symbol server selected."));

  if (cmd.sym_server()->state() != SymbolServer::State::kAuth) {
    return cmd_context->ReportError(Err("Server is not requesting authentication."));
  }

  if (cmd.args().size() == 0) {
    if (cmd.sym_server()->auth_type() != SymbolServer::AuthType::kOAuth) {
      return cmd_context->ReportError(Err("Unknown authentication type."));
    }

    cmd_context->Output(std::string("To authenticate, please supply an authentication "
                                    "token. You can retrieve a token from:\n\n") +
                        cmd.sym_server()->AuthInfo() +
                        "\n\nOnce you've retrieved a token, run 'auth <token>'");
    return;
  }

  cmd.sym_server()->Authenticate(
      cmd.args()[0], [name = cmd.sym_server()->name(), cmd_context](const Err& err) {
        if (!err.has_error()) {
          cmd_context->Output(std::string("Successfully authenticated with ") + name);
        } else {
          cmd_context->ReportError(
              Err(std::string("Authentication with ") + name + " failed: " + err.msg()));
        }
      });
}

}  // namespace

VerbRecord GetAuthVerbRecord() {
  return VerbRecord(&RunVerbAuth, {"auth"}, kAuthShortHelp, kAuthHelp, CommandGroup::kSymbol);
}

}  // namespace zxdb
