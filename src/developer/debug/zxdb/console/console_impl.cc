// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_impl.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef __Fuchsia__
#include <signal.h>
#include <termios.h>
#endif

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/console_suspend_token.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

const char* kHistoryFilename = ".zxdb_history";

}  // namespace

ConsoleImpl::ConsoleImpl(Session* session, line_input::ModalLineInput::Factory line_input_factory)
    : Console(session), line_input_(std::move(line_input_factory)), impl_weak_factory_(this) {
  line_input_.Init([this](std::string s) { ProcessInputLine(s); }, "[zxdb] ");

  // Set the line input completion callback that can know about our context. OK to bind |this| since
  // we own the line_input object.
  FillCommandContextCallback fill_command_context([this](Command* cmd) {
    context_.FillOutCommand(cmd);  // Ignore errors, this is for autocomplete.
  });
  line_input_.SetAutocompleteCallback([fill_command_context = std::move(fill_command_context)](
                                          std::string prefix) -> std::vector<std::string> {
    return GetCommandCompletions(prefix, fill_command_context);
  });

  // Cancel (ctrl-c) handling.
  line_input_.SetCancelCallback([this]() {
    if (line_input_.GetLine().empty()) {
      // Stop program execution. Do this by visibly typing the stop command so the user knows
      // what is happening.
      line_input_.SetCurrentInput("pause --clear-state");
      line_input_.OnInput(line_input::SpecialCharacters::kKeyEnter);
    } else {
      // Control-C with typing on the line just clears the current state.
      line_input_.SetCurrentInput(std::string());
    }
  });

  // EOF (ctrl-d) should exit gracefully.
  line_input_.SetEofCallback([this]() { Quit(); });

  // Set stdin to async mode or OnStdinReadable will block.
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
}

ConsoleImpl::~ConsoleImpl() {
  if (!SaveHistoryFile())
    Console::Output(Err("Could not save history file to $HOME/%s.\n", kHistoryFilename));
}

fxl::WeakPtr<ConsoleImpl> ConsoleImpl::GetImplWeakPtr() { return impl_weak_factory_.GetWeakPtr(); }

void ConsoleImpl::Init() {
  LoadHistoryFile();
  EnableInput();
}

void ConsoleImpl::LoadHistoryFile() {
  std::filesystem::path path(getenv("HOME"));
  if (path.empty())
    return;
  path /= kHistoryFilename;

  std::string data;
  if (!files::ReadFileToString(path, &data))
    return;

  auto history = fxl::SplitStringCopy(data, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (const std::string& cmd : history)
    line_input_.AddToHistory(cmd);
}

bool ConsoleImpl::SaveHistoryFile() {
  char* home = getenv("HOME");
  if (!home)
    return false;

  // We need to invert the order the deque has the entries.
  std::string history_data;
  const auto& history = line_input_.GetHistory();
  for (auto it = history.rbegin(); it != history.rend(); it++) {
    auto trimmed = fxl::TrimString(*it, " ");
    // We ignore empty entries or quit commands.
    if (trimmed.empty() || trimmed == "quit" || trimmed == "q" || trimmed == "exit") {
      continue;
    }

    history_data.append(trimmed).append("\n");
  }

  auto filepath = std::filesystem::path(home) / kHistoryFilename;
  return files::WriteFile(filepath, history_data.data(), history_data.size());
}

void ConsoleImpl::Output(const OutputBuffer& output) {
  // Since most operations are asynchronous, we have to hide the input line before printing anything
  // or it will get appended to whatever the user is typing on the screen.
  //
  // TODO(brettw) This can cause flickering. A more advanced system would do more fancy console
  // stuff to output above the input line so we'd never have to hide it.

  // Make sure stdout is in blocking mode since normal output won't expect non-blocking mode. We can
  // get in this state if stdin and stdout are the same underlying handle because the constructor
  // sets stdin to O_NONBLOCK so we can asynchronously wait for input.
  int old_bits = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (old_bits & O_NONBLOCK)
    fcntl(STDOUT_FILENO, F_SETFL, old_bits & ~O_NONBLOCK);

  // If input is disabled, there will be no prompt and we want to keep it off.
  if (InputEnabled())
    line_input_.Hide();

  output.WriteToStdout();

  if (InputEnabled())
    line_input_.Show();

  if (old_bits & O_NONBLOCK)
    fcntl(STDOUT_FILENO, F_SETFL, old_bits);
}

void ConsoleImpl::ModalGetOption(const line_input::ModalPromptOptions& options,
                                 OutputBuffer message, const std::string& prompt,
                                 line_input::ModalLineInput::ModalCompletionCallback cb) {
  // Input will normally be disabled before executing a command. When that command needs to read
  // input as part of its operation, we need to explicitly re-enable it.
  EnableInput();

  // Print the message from within the "will show" callback to ensure proper serialization if there
  // are multiple prompts pending.
  //
  // Okay to capture |this| because we own the line_input_.
  line_input_.ModalGetOption(options, prompt, std::move(cb),
                             [this, message = std::move(message)]() { Output(message); });
}

void ConsoleImpl::Quit() {
  line_input_.Hide();
  debug::MessageLoop::Current()->QuitNow();
}

void ConsoleImpl::Clear() {
  // We write directly instead of using Output because WriteToStdout expects to append '\n' to
  // outputs and won't flush it explicitly otherwise.
  if (InputEnabled())
    line_input_.Hide();

  const char ff[] = "\033c";  // Form feed.
  write(STDOUT_FILENO, ff, sizeof(ff));

  if (InputEnabled())
    line_input_.Show();
}

void ConsoleImpl::ProcessInputLine(const std::string& line, fxl::RefPtr<CommandContext> cmd_context,
                                   bool add_to_history) {
  if (!cmd_context)
    cmd_context = fxl::MakeRefCounted<ConsoleCommandContext>(this);

  Command cmd;
  if (line.empty()) {
    // Repeat the previous command, don't add to history.
    if (Err err = ParseCommand(previous_line_, &cmd); err.has_error())
      return cmd_context->ReportError(err);
  } else {
    Err err = ParseCommand(line, &cmd);
    if (add_to_history) {
      // Add to history even in the error case so the user can press "up" and fix it.
      line_input_.AddToHistory(line);
      previous_line_ = line;
    }
    if (err.has_error())
      return cmd_context->ReportError(err);
  }

  if (Err err = context_.FillOutCommand(&cmd); err.has_error())
    return cmd_context->ReportError(err);

  // Suspend input (if setting is enabled) and register for a callback to re-enable. This will have
  // the effect of blocking the UI for the duration of the command.
  auto ui_timeout =
      context().session()->system().settings().GetInt(ClientSettings::System::kUiTimeoutMs);
  if (ui_timeout > 0) {
    auto suspend_token = Console::get()->SuspendInput();
    cmd_context->SetConsoleCompletionObserver(fit::defer_callback([suspend_token]() {
      // Console::get()->Output("COMPLETE\n");
      suspend_token->Enable();
    }));

    // Some commands will take a long time to execute, re-enable the input if this happens.
    debug::MessageLoop::Current()->PostTimer(
        FROM_HERE, ui_timeout, [suspend_token, verb = cmd.verb()]() {
          if (suspend_token->enabled())
            return;  // Command already complete and input explicit re-enabled.

          // Otherwise the command is still running after the timeout. Print a message and re-enable
          // input so the user can get on with things.
          if (verb == Verb::kNone) {
            // Running a noun. Normally these won't take very long so we don't bother decoding the
            // name.
            Console::get()->Output("Command running in the background...\n");
          } else {
            Console::get()->Output(
                OutputBuffer(Syntax::kComment, "\"" + GetVerbRecord(verb)->aliases[0] +
                                                   "\" command running in the background...\n"));
          }
          suspend_token->Enable();
        });
  }

  DispatchCommand(cmd, cmd_context);

  if (cmd.thread() && cmd.verb() != Verb::kNone) {
    // Show the right source/disassembly for the next listing.
    context_.SetSourceAffinityForThread(cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
  }
}

fxl::RefPtr<ConsoleSuspendToken> ConsoleImpl::SuspendInput() {
  if (stdio_watch_.watching()) {
    line_input_.Hide();
    // Stop watching for stdin which will stop feeding input to the LineInput. Today, the LineInput
    // class doesn't suspend processing while hidden. If we didn't disable this watching, you would
    // still get commands executed even though you can't see your typing.
    //
    // Buffering here needs to be revisited because ideally we would make Control-C work to suspend
    // the synchronous mode, while also buffering the user typing while hidden.
    stdio_watch_.StopWatching();
  }
  return fxl::AdoptRef(new ConsoleSuspendToken);
}

void ConsoleImpl::EnableInput() {
  if (InputEnabled())
    return;

  // Callback for input passed to WatchFD().
  auto watch_fn = [this](int fd, bool readable, bool, bool error) {
    if (error) {
      // Happens most commonly on EOF.
      //
      // This handling isn't quite right when the input isn't the terminal. The error signal is
      // delivered in parallel with the data which means we'll get it as soon as the calling process
      // closes the pipe, whether or not we have read all the data.
      //
      // This means that if you do something like "echo help | zxdb", the first thing zxdb will
      // see will often be the error signal because the caller closed the pipe already, and no
      // input will be processed.
      //
      // To work around this, we may need go into a new "input drain mode" when we get the error.
      // This would ignore the async events on the file hand and read from stdin until read()
      // reports an error and then Quit(). The harder part is that mode will need to be integrated
      // the same way that watching stdin does, where we go back to the message loop for other work
      // to proceed, and stop/start according to Enable/DisableInput() calls.
      Quit();
      return;
    }

    if (!readable)
      return;

    // Dispatch one character at a time. The message loop will continue to notify us if the
    // input continues to be readable, so we'll get future messages for buffered input.
    //
    // The reason we want this is to avoid starving other work in the case that input is
    // buffered. If the user queued several "next" commands and we didn't return to the message
    // loop, for example, we wouldn't even get a chance to process any replies from the target
    // before issuing the following command.
    //
    // Note if we ever start dispatching more than one byte here, we should check InputEnabled()
    // in case the processing of the previous command has disabled input and no data is
    // expected.

    // Note that we need to handle EINTR when using this low-level syscall.
    char ch = 0;
    int bytes_read = 0;
    do {
      bytes_read = read(STDIN_FILENO, &ch, 1);
    } while (bytes_read == -1 && errno == EINTR);

    if (bytes_read > 0) {
      line_input_.OnInput(ch);
    } else {
      // Error or EOF.
      Quit();
    }
  };
  stdio_watch_ = debug::MessageLoop::Current()->WatchFD(debug::MessageLoop::WatchMode::kRead,
                                                        STDIN_FILENO, std::move(watch_fn));
  line_input_.Show();
}

bool ConsoleImpl::InputEnabled() const { return stdio_watch_.watching(); }

}  // namespace zxdb
