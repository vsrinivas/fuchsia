// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "garnet/lib/intel_pt_decode/decoder.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/stopwatch.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

#include "command_line_settings.h"

using namespace intel_processor_trace;

static constexpr char kUsageString[] =
    "insntrace_print [options]\n"
    "\n"
    "These options are required:\n"
    "--pt=FILE           PT input file\n"
    "--pt-list=FILE      Text file containing list of PT files\n"
    "                      Exactly one of --pt,--pt-list is required.\n"
    "--ids=FILE          An \"ids.txt\" file, which provides build-id\n"
    "                      to debug-info-containing ELF file (sideband data)\n"
    "                     May be specified multiple times.\n"
    "--ktrace=FILE       Name of the .ktrace file (sideband data)\n"
    "--map=FILE          Name of file containing mappings of ELF files to\n"
    "                      their load addresses (sideband data)\n"
    "                      This output currently comes from the dynamic "
    "linker\n"
    "                      when env var LD_TRACE=1 is set, and can be the "
    "output\n"
    "                      from loglistener.\n"
    "                      May be specified multiple times.\n"
    "\n"
    "The remaining options are optional.\n"
    "\n"
    "Input options:\n"
    "--elf=BINARY        ELF input PT files\n"
    "                      May be specified multiple times.\n"
    "                      This option is not useful with PIE executables,\n"
    "                      use sideband derived data instead.\n"
    "--kernel=PATH       Name of the kernel ELF file\n"
    "--kernel-cr3=CR3    CR3 value for the kernel (base 16)\n"
    "\n"
    "General output options:\n"
    "--output-format=raw|calls|chrome\n"
    "                    Default is \"calls\"\n"
    "--output-file=PATH\n"
    "                    For raw,calls the default is stdout.\n"
    "                    For chrome the default is tmp-ipt.json\n"
    "\n"
    "Options for \"--output-format=calls\":\n"
    "--pc                Dump numeric instruction addresses\n"
    "--insn              Dump instruction bytes\n"
    "--time=abs          Print absolute time\n"
    "--time=rel          Print relative time (trace begins at time 0)\n"
    "--report-lost       Report lost mtc,cyc packets\n"
    "\n"
    "Options for \"--output-format=chrome\":\n"
    "--id=ID             ID value to put in the output\n"
    "                      For cpu tracing, this is used to specify the cpu\n"
    "                      number if the PT dump is provided with --p.\n"
    "--view=cpu|process  Set the major axis of display, by cpu or process\n"
    "                      Chrome only understands processes and threads.\n"
    "                      Cpu view: processes are cpus,"
    " threads are processes.\n"
    "                      Process view: processes are processes,"
    " threads are cpus.\n"
    "                      The default is the cpu view.\n"
    "\n"
    "Logging options:\n"
    "  --quiet[=LEVEL]   Set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL] Set debug verbosity level\n"
    "  --log-file=FILE   Write log output to FILE.\n"
    "Quiet supersedes verbose if both are specified.\n"
    "Defined log levels:\n"
    "-n - verbosity level n\n"
    " 0 - INFO - this is the default level\n"
    " 1 - WARNING\n"
    " 2 - ERROR\n"
    " 3 - FATAL\n";

static void Usage(FILE* f) { fprintf(f, "%s", kUsageString); }

static bool ParseOption(const char* arg, fxl::StringView* out_name,
                        const char** out_value) {
  size_t len = strlen(arg);
  if (len < 2u || arg[0] != '-' || arg[1] != '-')
    return false;
  if (len == 2u) {
    // caller has to distinguish the "--" case
    return false;
  }

  // Note: The option name *must* be at least one character, so start at
  // position 3 -- "--=foo" will yield a name of "=foo" and no value.
  // (Passing a starting |pos| that's "too big" is OK.)
  const char* equals = strchr(arg + 3, '=');
  if (!equals) {
    *out_name = fxl::StringView(arg + 2);
    *out_value = "";
    return true;
  }

  *out_name = fxl::StringView(arg + 2, equals - arg - 2);
  *out_value = equals + 1;
  return true;
}

static int ParseArgv(int argc, char** argv, DecoderConfig* decoder_config,
                     CommandLineSettings* printer_config) {
  // While IWBN to use fxl::CommandLine here we need to support passing
  // multiple values for certain options (akin to -I options to the compiler).
  // So we do our own parsing, but we support the same syntax as fxl.

  int n;
  for (n = 1; n < argc; ++n) {
    fxl::StringView option;
    const char* value;

    if (strcmp(argv[n], "--") == 0)
      break;

    if (!ParseOption(argv[n], &option, &value))
      break;

    // TODO(dje): parsing of boolean options could be better

    if (option == "output-format") {
      if (strcmp(value, "raw") == 0) {
        printer_config->output_format = OutputFormat::kRaw;
      } else if (strcmp(value, "calls") == 0) {
        printer_config->output_format = OutputFormat::kCalls;
      } else if (strcmp(value, "chrome") == 0) {
        printer_config->output_format = OutputFormat::kChrome;
      } else {
        FXL_LOG(ERROR) << "Bad value for --output-format: " << value;
        return -1;
      }
      continue;
    }

    if (option == "output-file") {
      printer_config->output_file_name = value;
      continue;
    }

    if (option == "time") {
      if (strcmp(value, "abs") == 0) {
        printer_config->abstime = true;
      } else if (strcmp(value, "rel") == 0) {
        printer_config->abstime = false;
      } else {
        FXL_LOG(ERROR) << "Bad value for --time: " << value;
        return -1;
      }
      continue;
    }

    if (option == "elf") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty ELF file name";
        return -1;
      }
      decoder_config->elf_file_names.push_back(value);
      continue;
    }

    if (option == "pt") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty PT file name";
        return -1;
      }
      if (decoder_config->pt_file_name != "" ||
          decoder_config->pt_list_file_name != "") {
        FXL_LOG(ERROR) << "Only one of --pt/--pt-list supported";
        return -1;
      }
      decoder_config->pt_file_name = value;
      continue;
    }

    if (option == "pt-list") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty PT-list file name";
        return -1;
      }
      if (decoder_config->pt_file_name != "" ||
          decoder_config->pt_list_file_name != "") {
        FXL_LOG(ERROR) << "Only one of --pt/--pt-list supported";
        return -1;
      }
      decoder_config->pt_list_file_name = value;
      continue;
    }

    if (option == "pc") {
      printer_config->dump_pc = true;
      continue;
    }

    if (option == "insn") {
      printer_config->dump_insn = true;
      continue;
    }

    if (option == "report-lost") {
      printer_config->report_lost_mtc_cyc = true;
      continue;
    }

    if (option == "id") {
      if (!fxl::StringToNumberWithError<uint32_t>(
              fxl::StringView(value), &printer_config->id, fxl::Base::k16)) {
        FXL_LOG(ERROR) << "Not a hex number: " << value;
        return -1;
      }
      continue;
    }

    if (option == "view") {
      if (strcmp(value, "cpu") == 0) {
        printer_config->view = OutputView::kCpu;
      } else if (strcmp(value, "process") == 0) {
        printer_config->view = OutputView::kProcess;
      } else {
        FXL_LOG(ERROR) << "Bad value for --view: " << value;
        return -1;
      }
      continue;
    }

    if (option == "kernel") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty kernel file name";
        return -1;
      }
      decoder_config->kernel_file_name = value;
      continue;
    }

    if (option == "kernel-cr3") {
      if (!fxl::StringToNumberWithError<uint64_t>(fxl::StringView(value),
                                                  &decoder_config->kernel_cr3,
                                                  fxl::Base::k16)) {
        FXL_LOG(ERROR) << "Not a valid cr3 number: " << value;
        return -1;
      }
      continue;
    }

    if (option == "ids") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty ids file name";
        return -1;
      }
      decoder_config->ids_file_names.push_back(value);
      continue;
    }

    if (option == "ktrace") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty ktrace file name";
        return -1;
      }
      decoder_config->ktrace_file_name = value;
      continue;
    }

    if (option == "map") {
      if (strlen(value) == 0) {
        FXL_LOG(ERROR) << "Empty map file name";
        return -1;
      }
      decoder_config->map_file_names.push_back(value);
      continue;
    }

    if (option == "verbose") {
      // already processed by fxl::SetLogSettingsFromCommandLine
      continue;
    }

    FXL_LOG(ERROR) << "Unrecognized option: " << option;
    return -1;
  }

  if (n < argc && strcmp(argv[n], "--") == 0) {
    ++n;
  }

  if (decoder_config->pt_file_name == "" &&
      decoder_config->pt_list_file_name == "") {
    FXL_LOG(ERROR) << "One of --pt=FILE, --pt-list=FILE must be specified";
    return -1;
  }
  if (decoder_config->ktrace_file_name == "") {
    FXL_LOG(WARNING) << "missing --ktrace=FILE, output may be limited";
  }
  if (decoder_config->ids_file_names.size() == 0) {
    FXL_LOG(WARNING) << "missing --ids=FILE, output will be limited";
  }
  if (decoder_config->map_file_names.size() == 0) {
    FXL_LOG(WARNING) << "missing --map=FILE, output will be limited";
  }

  return n;
}

int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help")) {
    Usage(stdout);
    return EXIT_SUCCESS;
  }

  DecoderConfig decoder_config;
  CommandLineSettings printer_config;
  int n = ParseArgv(argc, argv, &decoder_config, &printer_config);
  if (n < 0)
    return EXIT_FAILURE;

  if (n != argc) {
    FXL_LOG(ERROR) << "No positional parameters";
    return EXIT_FAILURE;
  }

  fxl::Stopwatch stop_watch;
  stop_watch.Start();

  auto decoder = DecoderState::Create(decoder_config);
  if (!decoder) {
    FXL_LOG(ERROR) << "Error creating decoder";
    return EXIT_FAILURE;
  }

  uint64_t total_insns;
  if (printer_config.output_format == OutputFormat::kRaw) {
    auto printer =
        RawPrinter::Create(decoder.get(), printer_config.ToRawPrinterConfig());
    if (!printer) {
      FXL_LOG(ERROR) << "Error creating printer";
      return EXIT_FAILURE;
    }
    total_insns = printer->PrintFiles();
  } else if (printer_config.output_format == OutputFormat::kCalls) {
    auto printer = CallPrinter::Create(decoder.get(),
                                       printer_config.ToCallPrinterConfig());
    if (!printer) {
      FXL_LOG(ERROR) << "Error creating printer";
      return EXIT_FAILURE;
    }
    total_insns = printer->PrintFiles();
  } else {
    FXL_LOG(ERROR) << "Invalid output format\n";
    return EXIT_FAILURE;
  }

  fxl::TimeDelta delta = stop_watch.Elapsed();
  int64_t seconds = delta.ToSeconds();
  int milliseconds = delta.ToMilliseconds() % 1000;
  FXL_LOG(INFO) << fxl::StringPrintf(
      "%" PRIu64 " instructions processed in %" PRId64 ".%03d seconds\n",
      total_insns, seconds, milliseconds);

  return EXIT_SUCCESS;
}
