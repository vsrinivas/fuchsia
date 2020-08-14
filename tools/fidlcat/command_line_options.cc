// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/command_line_options.h"

#include <lib/cmdline/args_parser.h>
#include <lib/syslog/cpp/log_settings.h>
#include <regex.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "src/lib/fxl/strings/string_number_conversions.h"
#include "tools/fidlcat/lib/decode_options.h"

namespace fidlcat {

int constexpr kMinColumns = 80;

const char* const kHelpIntro = R"(fidlcat [ <options> ] [ command [args] ]

  fidlcat will run the specified command until it exits.  It will intercept and
  record all fidl calls invoked by the process.  The command may be of the form
  "run <component URL>", in which case the given component will be launched.

  fidlcat will return the code 1 if its parameters are invalid.

  fidlcat expects a debug agent to be running on the target device.  It will
  return the code 2 if it cannot connect to the debug agent.

Options:

)";

const char* const kRemoteHostHelp = R"(  --connect
      The host and port of the debug agent running on the target Fuchsia
      instance, of the form [<ipv6_addr>]:port.)";

const char* const kSymbolIndexHelp = R"(  --symbol-index=<path>
      Populates --ids-txt and --build-id-dir using the given symbol-index file,
      which defaults to ~/.fuchsia/debug/symbol-index. The file should be
      created and maintained by the "symbol-index" host tool.)";

const char* const kBuildIdDirHelp = R"(  --build-id-dir=<path>
      Adds the given directory to the symbol search path. Multiple
      --build-id-dir switches can be passed to add multiple directories.
      The directory must have the same structure as a .build-id directory,
      that is, each symbol file lives at xx/yyyyyyyy.debug where xx is
      the first two characters of the build ID and yyyyyyyy is the rest.
      However, the name of the directory doesn't need to be .build-id.)";

const char* const kSymbolServerHelp = R"(  --symbol-server=<url>
      Adds the given URL to symbol servers. Symbol servers host the debug
      symbols for prebuilt binaries and dynamic libraries.)";

const char* const kSymbolPathHelp = R"(  --symbol-path=<path>
  -s <path>
      Adds the given directory or file to the symbol search path. Multiple
      -s switches can be passed to add multiple locations. When a directory
      path is passed, the directory will be enumerated non-recursively to
      index all ELF files. When a file is passed, it will be loaded as an ELF
      file (if possible).)";

const char* const kSymbolCacheHelp = R"(  --symbol-cache=<path>
      Directory where we can keep a symbol cache. If a symbol server has been
      specified, downloaded symbols will be stored in this directory. The
      directory structure will be the same as a .build-id directory, and
      symbols will be read from this location as though you had specified
      "--build-id-dir=<path>".)";

const char* const kFidlIrPathHelp = R"(  --fidl-ir-path=<path>|@argfile
      Adds the given path as a repository for FIDL IR, in the form of .fidl.json
      files.  Passing a file adds the given file.  Passing a directory adds all
      of the .fidl.json files in that directory and any directory transitively
      reachable from there. An argfile contains a newline-separated list of
      .fidl.json files relative to the directory containing the argfile; passing
      an argfile (starting with the '@' character) adds all files listed in that
      argfile.  This switch can be passed multiple times to add multiple
      locations.)";

const char* const kIdsTxtHelp = R"(  --ids-txt=<path>
      Adds the given file to the symbol search path. Multiple --ids-txt
      switches can be passed to add multiple files. The file, typically named
      "ids.txt", serves as a mapping from build ID to symbol file path and
      should contain multiple lines in the format of "<build ID> <file path>".)";

const char* const kQuitAgentOnExitHelp = R"(  --quit-agent-on-exit
      Will send a quit message to a connected debug agent in order for it to
      shutdown. This is so that fidlcat doesn't leak unwanted debug agents on
      "on-the-fly" debugging sessions.)";

const char* const kFromHelp = R"(  --from=<source>
      This option must be used at most once.
      Source can be:
      --from=device This is the default input. The input comes from the live monitoring of one or
                    several processes.
                    At least one of '--remote-pid', '--remote-name', 'run' must be specified.
      --from=<path> The input comes from a previously recorded session (protobuf format). Path gives
                    the name of the file to read. If path is '-' then the standard input is used.)";

const char* const kToHelp = R"(  --to=<path>
      Save the session using protobuf in the specified file. All events are
      saved including the messages which have been filtered out by --messages
      or --exclude-messages.)";

const char* const kFormatHelp = R"(  --format=<output>
      This option must be used at most once.
      The output format can be:
      --format=pretty    The session is pretty printed (with colors).
                         This is the default output is --with is not used.
      --format=json      The session is printed using a json format.
      --format=textproto The session is printed using a text protobuf format.
      --format=none      Nothing is displayed on the standard output (this option only makes sense
                         when used with --to=<path> or with --with).
                         When there is no output, fidlcat is much faster (this is better when you
                         want to monitor real time components).
                         This is the default output is --with is used.)";

const char* const kWithHelp = R"(These options can be used several times.
  --with=summary
      At the end of the session, a summary of the session is displayed on the standard output.
  --with=summary=<path>
      Like --with=summary but the result is stored into the file specified by <path>.
  --with=top
      At the end of the session, generate a view that groups the output by process, protocol, and
      method. The groups are sorted by number of events, so groups with more associated events are
      listed earlier.
  --with=top=<path>
      Like --with=top but the result is stored into the file specified by <path>..)";

const char* const kCompareHelp = R"(  --compare=<path>
      Compare output with the one stored in the given file)";

const char* const kWithProcessInfoHelp = R"(  --with-process-info
      Display the process name, process id and thread id on each line.)";

const char* const kStackHelp = R"(  --stack=<value>
      The amount of stack frame to display:
      - 0: no stack (default value)
      - 1: call site (1 to 4 levels)
      - 2: full stack frame (adds some overhead))";

const char* const kSyscallFilterHelp = R"(  --syscalls
      A regular expression which selects the syscalls to decode and display.
      Can be passed multiple times.
      By default, only zx_channel_.* syscalls are displayed.
      To display all the syscalls, use: --syscalls=".*")";

const char* const kExcludeSyscallFilterHelp = R"(  --exclude-syscalls
      A regular expression which selects the syscalls to not decode and display.
      Can be passed multiple times.
      To be displayed, a syscall must verify --syscalls and not verify
      --exclude-syscalls.
      To display all the syscalls but the zx_handle syscalls, use:
        --syscalls=".*" --exclude-syscalls="zx_handle_.*")";

const char* const kMessageFilterHelp = R"(  --messages
      A regular expression which selects the messages to display.
      To display a message, the method name must satisfy the regexp.
      This option can be specified multiple times.
      Message filtering works on the method's fully qualified name.)";

const char* const kExcludeMessageFilterHelp = R"(  --exclude-messages
      A regular expression which selects the messages to not display.
      If a message method name satisfy the regexp, the message is not displayed
      (even if it satifies --messages).
      This option can be specified multiple times.
      Message filtering works on the method's fully qualified name.)";

const char* const kTriggerFilterHelp = R"(  --trigger
      Start displaying messages and syscalls only when a message for which the
      method name satisfies the filter is found.
      This option can be specified multiple times.
      Message filtering works on the method's fully qualified name.)";

const char* const kDumpMessagesHelp = R"(  --dump-messages
      Always display the message binary dump even if we can decode the message.
      By default the dump is only displayed if we can't decode the message.)";

const char* const kColorsHelp = R"(  --colors=[never|auto|always]
      For pretty print, use colors:
      - never
      - auto: only if running in a terminal (default value)
      - always)";

const char* const kColumnsHelp = R"(  --columns=<size>
      For pretty print, width of the display. By default, on a terminal, use
      the terminal width.)";

const char* const kVerbosityHelp = R"(  --verbose=<number or log level>
      The log verbosity.  Legal values are "info", "warning", "error", "fatal",
      or a number, starting from 0. Extra verbosity comes with higher levels)";

const char* const kQuietHelp = R"(  --quiet=<number or log level>
      The log verbosity.  Legal values are "info", "warning", "error", "fatal",
      or a number, starting from 0. Extra verbosity comes with lower levels.)";

const char* const kLogFileHelp = R"(  --log-file=<pathspec>
      The name of a file to which the log should be written.)";

const char* const kRemotePidHelp = R"(  --remote-pid
      The koid of the remote process. Can be passed multiple times.)";

const char* const kRemoteNameHelp = R"(  --remote-name=<regexp>
  -f <regexp>
      Adds a filter to the default job that will cause fidlcat to attach
      to existing or future processes whose names match this regexp.
      For example:
          --remote-name echo_client.*.cmx
          --remote-name echo_client
      Multiple filters can be specified to match more than one process.)";

const char* const kExtraNameHelp = R"(  --extra-name=<regexp>
      Like --remote-name, it monitors some processes. However, for these
      processes, monitoring starts only when one of of the "--remote-name"
      process is launched. Also, fidlcat stops when the last "--remote-name"
      process stops (even if some "--extra-name" processes are still
      monitored). You must specify at least one filter with --remote-name if
      you use this option (without --remote-name, nothing would be displayed).)";

const char* const kHelpHelp = R"(  --help
  -h
      Prints all command-line switches.)";

// Sets the process log settings.  The |level| is the value of the setting (as
// passed to --quiet or --verbose), |multiplier| is a value by which a numerical
// setting will be multiplied (basically, -1 for verbose and 1 for quiet), and
// |settings| contains the output.
bool SetLogSettings(const std::string& level, int multiplier, syslog::LogSettings* settings) {
  if (level == "trace") {
    settings->min_log_level = syslog::LOG_TRACE;
  } else if (level == "debug") {
    settings->min_log_level = syslog::LOG_DEBUG;
  } else if (level == "info") {
    settings->min_log_level = syslog::LOG_INFO;
  } else if (level == "warning") {
    settings->min_log_level = syslog::LOG_WARNING;
  } else if (level == "error") {
    settings->min_log_level = syslog::LOG_ERROR;
  } else if (level == "fatal") {
    settings->min_log_level = syslog::LOG_FATAL;
  } else if (fxl::StringToNumberWithError(level, &settings->min_log_level)) {
    settings->min_log_level =
        syslog::LOG_INFO + (multiplier * (multiplier > 0 ? syslog::LogSeverityStepSize
                                                         : syslog::LogVerbosityStepSize));

  } else {
    return false;
  }
  return true;
}

cmdline::Status ProcessLogOptions(const CommandLineOptions* options) {
  syslog::LogSettings settings;
  if (options->verbose) {
    if (!SetLogSettings(*options->verbose, -1, &settings)) {
      return cmdline::Status::Error("Unable to parse verbose setting \"" + *options->verbose +
                                    "\"");
    }
  }
  if (options->quiet) {
    if (!SetLogSettings(*options->quiet, 1, &settings)) {
      return cmdline::Status::Error("Unable to parse quiet setting \"" + *options->quiet + "\"");
    }
  }
  if (options->log_file) {
    settings.log_file = *options->log_file;
  }
  syslog::SetLogSettings(settings);
  return cmdline::Status::Ok();
}

std::string ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                             DecodeOptions* decode_options, DisplayOptions* display_options,
                             std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  // Debug agent options:
  parser.AddSwitch("connect", 'r', kRemoteHostHelp, &CommandLineOptions::connect);
  parser.AddSwitch("symbol-index", 0, kSymbolIndexHelp, &CommandLineOptions::symbol_index_files);
  parser.AddSwitch("build-id-dir", 0, kBuildIdDirHelp, &CommandLineOptions::build_id_dirs);
  parser.AddSwitch("symbol-server", 0, kSymbolServerHelp, &CommandLineOptions::symbol_servers);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp, &CommandLineOptions::symbol_paths);
  parser.AddSwitch("symbol-cache", 0, kSymbolCacheHelp, &CommandLineOptions::symbol_cache);
  // Fidlcat system options:
  parser.AddSwitch("fidl-ir-path", 0, kFidlIrPathHelp, &CommandLineOptions::fidl_ir_paths);
  parser.AddSwitch("ids-txt", 0, kIdsTxtHelp, &CommandLineOptions::ids_txts);
  parser.AddSwitch("quit-agent-on-exit", 0, kQuitAgentOnExitHelp,
                   &CommandLineOptions::quit_agent_on_exit);
  // Input option:
  parser.AddSwitch("from", 0, kFromHelp, &CommandLineOptions::from);
  // Session save option:
  parser.AddSwitch("to", 0, kToHelp, &CommandLineOptions::to);
  // Format (output) option:
  parser.AddSwitch("format", 0, kFormatHelp, &CommandLineOptions::format);
  // Extra generation:
  parser.AddSwitch("with", 0, kWithHelp, &CommandLineOptions::extra_generation);
  // Session comparison option:
  parser.AddSwitch("compare", 'c', kCompareHelp, &CommandLineOptions::compare_file);
  // Display options:
  parser.AddSwitch("with-process-info", 0, kWithProcessInfoHelp,
                   &CommandLineOptions::with_process_info);
  parser.AddSwitch("stack", 0, kStackHelp, &CommandLineOptions::stack_level);
  parser.AddSwitch("syscalls", 0, kSyscallFilterHelp, &CommandLineOptions::syscall_filters);
  parser.AddSwitch("exclude-syscalls", 0, kExcludeSyscallFilterHelp,
                   &CommandLineOptions::exclude_syscall_filters);
  parser.AddSwitch("messages", 0, kMessageFilterHelp, &CommandLineOptions::message_filters);
  parser.AddSwitch("exclude-messages", 0, kExcludeMessageFilterHelp,
                   &CommandLineOptions::exclude_message_filters);
  parser.AddSwitch("trigger", 0, kTriggerFilterHelp, &CommandLineOptions::trigger_filters);
  parser.AddSwitch("dump-messages", 0, kDumpMessagesHelp, &CommandLineOptions::dump_messages);
  parser.AddSwitch("colors", 0, kColorsHelp, &CommandLineOptions::colors);
  parser.AddSwitch("columns", 0, kColumnsHelp, &CommandLineOptions::columns);
  // Logging options:
  parser.AddSwitch("verbose", 'v', kVerbosityHelp, &CommandLineOptions::verbose);
  parser.AddSwitch("quiet", 'q', kQuietHelp, &CommandLineOptions::quiet);
  parser.AddSwitch("log-file", 0, kLogFileHelp, &CommandLineOptions::log_file);
  // Monitoring options:
  parser.AddSwitch("remote-pid", 'p', kRemotePidHelp, &CommandLineOptions::remote_pid);
  parser.AddSwitch("remote-name", 'f', kRemoteNameHelp, &CommandLineOptions::remote_name);
  parser.AddSwitch("extra-name", 0, kExtraNameHelp, &CommandLineOptions::extra_name);
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status.error_message();
  }

  status = ProcessLogOptions(options);
  if (status.has_error()) {
    return status.error_message();
  }

  bool device = options->from.empty() || (options->from == "device");
  bool watch = !options->remote_name.empty() || !options->remote_pid.empty() ||
               (std::find(params->begin(), params->end(), "run") != params->end());

  if (requested_help || (device && !watch) ||
      (!options->extra_name.empty() && options->remote_name.empty())) {
    status = cmdline::Status::Error(kHelpIntro + parser.GetHelp());
    return status.error_message();
  }

  decode_options->stack_level = options->stack_level;
  if (options->syscall_filters.empty()) {
    regex_t r;
    regcomp(&r, "zx_channel_.*", REG_EXTENDED);
    decode_options->syscall_filters.emplace_back(std::make_unique<Regex>(r));
    regcomp(&r, "zx_handle_close", REG_EXTENDED);
    decode_options->syscall_filters.emplace_back(std::make_unique<Regex>(r));
    regcomp(&r, "zx_handle_close_many", REG_EXTENDED);
    decode_options->syscall_filters.emplace_back(std::make_unique<Regex>(r));
  } else if ((options->syscall_filters.size() != 1) || (options->syscall_filters[0] != ".*")) {
    for (const auto& filter : options->syscall_filters) {
      regex_t r;
      if (regcomp(&r, filter.c_str(), REG_EXTENDED) == 0) {
        decode_options->syscall_filters.emplace_back(std::make_unique<Regex>(r));
      } else {
        return "Bad filter for --syscalls: " + filter;
      }
    }
  }
  for (const auto& filter : options->exclude_syscall_filters) {
    regex_t r;
    if (regcomp(&r, filter.c_str(), REG_EXTENDED) == 0) {
      decode_options->exclude_syscall_filters.emplace_back(std::make_unique<Regex>(r));
    } else {
      return "Bad filter for --exclude-syscalls: " + filter;
    }
  }
  for (const auto& filter : options->message_filters) {
    regex_t r;
    if (regcomp(&r, filter.c_str(), REG_EXTENDED) == 0) {
      decode_options->message_filters.emplace_back(std::make_unique<Regex>(r));
    } else {
      return "Bad filter for --messages: " + filter;
    }
  }
  for (const auto& filter : options->exclude_message_filters) {
    regex_t r;
    if (regcomp(&r, filter.c_str(), REG_EXTENDED) == 0) {
      decode_options->exclude_message_filters.emplace_back(std::make_unique<Regex>(r));
    } else {
      return "Bad filter for --exclude-messages: " + filter;
    }
  }
  for (const auto& filter : options->trigger_filters) {
    regex_t r;
    if (regcomp(&r, filter.c_str(), REG_EXTENDED) == 0) {
      decode_options->trigger_filters.emplace_back(std::make_unique<Regex>(r));
    } else {
      return "Bad filter for --trigger: " + filter;
    }
  }

  decode_options->save = options->to;

  display_options->with_process_info = options->with_process_info;

  struct winsize term_size;
  term_size.ws_col = 0;
  int ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, &term_size);
  if (options->columns == 0) {
    display_options->columns = term_size.ws_col;
    display_options->columns = std::max(display_options->columns, kMinColumns);
  } else {
    display_options->columns = options->columns;
  }

  display_options->dump_messages = options->dump_messages;

  if (!options->from.empty() && (options->from != "device")) {
    decode_options->input_mode = InputMode::kFile;
  }

  if ((!options->format.has_value() && options->extra_generation.empty()) ||
      (options->format.has_value() && (options->format.value() == "pretty"))) {
    decode_options->output_mode = OutputMode::kStandard;
    display_options->pretty_print = true;
    display_options->needs_colors =
        ((options->colors == "always") || ((options->colors == "auto") && (ioctl_result != -1))) &&
        !(options->compare_file.has_value());
  } else if (options->format.has_value()) {
    if (options->format.value() == "json") {
      decode_options->output_mode = OutputMode::kStandard;
    } else if (options->format.value() == "textproto") {
      decode_options->output_mode = OutputMode::kTextProtobuf;
    } else if (options->format.value() == "none") {
    } else {
      return "Invalid format " + options->format.value() + " for option --format.";
    }
  }

  display_options->extra_generation_needs_colors =
      ((options->colors == "always") || ((options->colors == "auto") && (ioctl_result != -1)));

  for (const auto& extra_generation : options->extra_generation) {
    if (extra_generation == "summary") {
      display_options->AddExtraGeneration(ExtraGeneration::Kind::kSummary, "");
    } else if (extra_generation.find("summary=") == 0) {
      display_options->AddExtraGeneration(ExtraGeneration::Kind::kSummary,
                                          extra_generation.substr(8));
    } else if (extra_generation == "top") {
      display_options->AddExtraGeneration(ExtraGeneration::Kind::kTop, "");
    } else if (extra_generation.find("top=") == 0) {
      display_options->AddExtraGeneration(ExtraGeneration::Kind::kTop, extra_generation.substr(4));
    } else {
      return "Invalid generation " + extra_generation + " for option --with.";
    }
  }

  return "";
}

namespace {

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

}  // namespace

void ExpandFidlPathsFromOptions(std::vector<std::string> cli_ir_paths,
                                std::vector<std::string>& paths,
                                std::vector<std::string>& bad_paths) {
  // Strip out argfiles before doing path processing.
  for (int64_t i = cli_ir_paths.size() - 1; i >= 0; i--) {
    std::string& path = cli_ir_paths[i];
    if (path.compare(0, 1, "@") == 0) {
      std::filesystem::path real_path(path.substr(1));
      auto enclosing_directory = real_path.parent_path();
      std::string file = path.substr(1);
      cli_ir_paths.erase(cli_ir_paths.begin() + i);

      std::ifstream infile(file, std::ifstream::in);
      if (!infile.good()) {
        bad_paths.push_back(file);
        continue;
      }

      std::string jsonfile_path;
      while (infile >> jsonfile_path) {
        if (std::filesystem::path(jsonfile_path).is_relative()) {
          jsonfile_path = enclosing_directory.string() +
                          std::filesystem::path::preferred_separator + jsonfile_path;
        }

        paths.push_back(jsonfile_path);
      }
    }
  }

  std::set<std::string> checked_dirs;
  // Repeat until cli_ir_paths is empty:
  //  If it is a directory, add the directory contents to the cli_ir_paths.
  //  If it is a .fidl.json file, add it to |paths|.
  while (!cli_ir_paths.empty()) {
    std::string current_string = cli_ir_paths.back();
    cli_ir_paths.pop_back();
    std::filesystem::path current_path = current_string;
    if (std::filesystem::is_directory(current_path)) {
      for (auto& dir_ent : std::filesystem::directory_iterator(current_path)) {
        std::string ent_name = dir_ent.path().string();
        if (std::filesystem::is_directory(ent_name)) {
          auto found = checked_dirs.find(ent_name);
          if (found == checked_dirs.end()) {
            checked_dirs.insert(ent_name);
            cli_ir_paths.push_back(ent_name);
          }
        } else if (EndsWith(ent_name, ".fidl.json")) {
          paths.push_back(dir_ent.path());
        }
      }
    } else if (std::filesystem::is_regular_file(current_path) &&
               EndsWith(current_string, ".fidl.json")) {
      paths.push_back(current_string);
    } else {
      bad_paths.push_back(current_string);
    }
  }
}

}  // namespace fidlcat
