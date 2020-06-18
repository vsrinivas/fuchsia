// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpidump.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <lib/cmdline/args_parser.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <fbl/array.h>
#include <fbl/span.h>
#include <fbl/unique_fd.h>

#include "fbl/string_printf.h"
#include "lib/fidl/llcpp/vector_view.h"

namespace acpidump {

using llcpp::fuchsia::hardware::acpi::Acpi;
using llcpp::fuchsia::hardware::acpi::TableInfo;

const char kAcpiDevicePath[] = "/dev/sys/platform/acpi";

// Set up an argument parser. We attempt to follow the conventions of
// Linux's "acpidump" commands where possible, though we also add long
// versions of the switches.
static std::unique_ptr<cmdline::ArgsParser<Args>> MakeArgsParser() {
  auto parser = std::make_unique<cmdline::ArgsParser<Args>>();
  parser->AddSwitch("summary", 's', "Summarise table names, but don't show content.",
                    &Args::table_names_only);
  parser->AddSwitch("binary", 'b', "Dump raw binary data format.", &Args::dump_raw);
  parser->AddSwitch("help", 'h', "Show this help message.", &Args::show_help);
  parser->AddSwitch("table", 't', "Only dump the named table.", &Args::table);
  return parser;
}

void PrintUsage(const char* prog_name) {
  // cmdline::ArgParser has only a very simple usage screen, so we just
  // print out our own.
  fprintf(stderr,
          "usage:\n"
          "%s [options]\n"
          "\n"
          "Dumps raw system ACPI tables.\n"
          "\n"
          "Options:\n"
          "    -s                   : Summarise table names, but don't show content.\n"
          "    -t <table name>      : Only dump the named table.\n"
          "    -b                   : Dump raw binary data format.\n"
          "                           Requires a table name to be specified.\n"
          "    -h, --help           : Show this help message.\n",
          prog_name);
}

bool ParseArgs(fbl::Span<const char* const> args, Args* result) {
  *result = Args{};

  // Parse the args.
  std::vector<std::string> params;
  cmdline::Status status = MakeArgsParser()->Parse(args.size(), args.data(), result, &params);
  if (!status.ok()) {
    fprintf(stderr, "Could not parse arguments: %s\n", status.error_message().c_str());
    return false;
  }

  // Ensure no additional args were given.
  if (params.size() > 0) {
    fprintf(stderr, "Unknown argument: '%s'\n", params[0].c_str());
    return false;
  }

  // Check for incompatible arguments.
  if (result->table_names_only) {
    if (result->dump_raw) {
      fprintf(stderr, "Error: Cannot summarise and dump as raw.\n");
      return false;
    }
    if (result->table.has_value()) {
      fprintf(stderr, "Error: Cannot summarise a single table only.\n");
      return false;
    }
  }
  if (result->dump_raw && !result->table.has_value()) {
    fprintf(stderr, "Error: Dumping binary requires specifying a table name.\n");
    return false;
  }

  return true;
}

// Convert a fidl::Array<uint8_t, n> type to a std::string.
template <uint64_t N>
std::string SignatureToString(const fidl::Array<uint8_t, N>& array) {
  return std::string(reinterpret_cast<const char*>(array.data()), array.size());
}

// Print the list of table names.
//
// We attempt to copy the same output as Linux's "acpidump" command.
void PrintTableNames(const fidl::VectorView<TableInfo>& entries) {
  for (size_t i = 0; i < entries.count(); i++) {
    printf("ACPI: %s %06x\n", SignatureToString(entries[i].name).c_str(), entries[i].size);
  }
}

// Fetch raw data for a table.
zx_status_t FetchTable(const zx::channel& channel, const TableInfo& table,
                       fbl::Array<uint8_t>* data) {
  // Allocate a VMO for the read.
  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(table.size, /*options=*/0, &vmo); status != ZX_OK) {
    return status;
  }

  // Make a copy to send to the driver.
  zx::vmo vmo_copy;
  if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy); status != ZX_OK) {
    return status;
  }

  // Fetch the data.
  Acpi::ResultOf::ReadNamedTable result =
      Acpi::Call::ReadNamedTable(channel.borrow(), table.name, 0, std::move(vmo_copy));
  if (!result.ok()) {
    return result.status();
  }

  // Copy the data into memory.
  uint32_t size = result.value().result.response().size;
  auto table_data = fbl::Array<uint8_t>(new uint8_t[size], size);
  if (zx_status_t status = vmo.read(table_data.data(), 0, size); status != ZX_OK) {
    return status;
  }

  *data = std::move(table_data);
  return ZX_OK;
}

// Print the given data directly to stdout.
void PrintRaw(const fbl::Array<uint8_t>& data) { fwrite(data.begin(), 1, data.size(), stdout); }

// Print the ACPI table |name|.
//
// We attempt to duplicate the formatting of the native Linux "acpidump"
// command to allow user scripts, "xxd" invocations etc to work without
// modification.
//
// Example output:
//
// DSDT
//     0000: 44 53 44 54 B4 1F 00 00 01 9B 42 4F 43 48 53 20  DSDT......BOCHS
//     0010: 42 58 50 43 44 53 44 54 01 00 00 00 42 58 50 43  BXPCDSDT....BXPC
// (...)
void PrintHex(const char* name, const fbl::Array<uint8_t>& data) {
  // Print table name.
  printf("%s\n", name);

  // Print hex dump of data.
  for (size_t address = 0; address < data.size(); address += 16) {
    // Print '    1234:' (4x ' ' padding, 4x '0' padding).
    printf("%8s: ", fbl::StringPrintf("%04lX", address).c_str());
    // Print ' AA BB CC DD' (16x bytes as hex)
    size_t i;
    for (i = 0; i < 16 && i + address < data.size(); i++) {
      printf("%02X ", data[i + address]);
    }
    // Print any extra padding for the last line.
    for (; i < 16; i++) {
      printf("   ");
    }
    // Print gap between hex values and ASCII.
    putchar(' ');
    // Print ASCII chars.
    for (i = 0; i < 16 && i + address < data.size(); i++) {
      putchar(isprint(data[i + address]) ? data[i + address] : '.');
    }
    putchar('\n');
  }
  putchar('\n');
}

// Open the ACPI device, waiting until it appears if necessary (e.g., if we run shortly
// after system boot).
static fbl::unique_fd OpenAcpiDevice() {
  zx::duration poll_delay = zx::msec(1);
  bool first_poll = true;

  while (true) {
    // Attempt to open the device.
    fbl::unique_fd fd{open(kAcpiDevicePath, O_RDWR)};
    if (fd.is_valid()) {
      return fd;
    }

    // If we have an error (other than "file not found") print an error and return.
    if (errno != ENOENT) {
      fprintf(stderr, "Failed to open '%s': %s\n", kAcpiDevicePath, strerror(errno));
      return fbl::unique_fd();
    }

    // If we couldn't open it because it doesn't exist, it might just be that
    // ACPI hasn't been enumerated yet. Poll the file every so often.
    //
    // TODO(dgreenaway): Instead of polling, using the Watch API.
    if (first_poll) {
      fprintf(stderr, "ACPI device '%s' not found. Waiting for it to appear...\n", kAcpiDevicePath);
      first_poll = false;
    }

    // Poll with exponential backoff, up to a 1 second polling interval.
    zx::nanosleep(zx::deadline_after(poll_delay));
    poll_delay = std::min(poll_delay * 2, zx::sec(1));
  }
}

zx_status_t AcpiDump(const Args& args) {
  // Open up channel to ACPI device.
  fbl::unique_fd fd = OpenAcpiDevice();
  if (!fd.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }
  fdio_cpp::FdioCaller dev(std::move(fd));

  // List ACPI entries.
  Acpi::ResultOf::ListTableEntries result = Acpi::Call::ListTableEntries(dev.channel());
  if (!result.ok()) {
    fprintf(stderr, "Could not list ACPI table entries: %s.\n",
            zx_status_get_string(result.status()));
    return result.status();
  }
  if (result.value().result.is_err()) {
    fprintf(stderr, "Call to list ACPI table entries failed: %s.\n",
            zx_status_get_string(result.value().result.err()));
    return result.value().result.err();
  }

  // Print summary if requested.
  auto& entries = result.value().result.response().entries;
  if (args.table_names_only) {
    PrintTableNames(entries);
    return 0;
  }

  // Print each table if requested.
  bool found_table = false;
  for (auto table : entries) {
    // Skip over tables we should ignore.
    if (args.table.has_value() &&
        std::string_view(reinterpret_cast<const char*>(table.name.begin()), table.name.size()) !=
            args.table.value()) {
      continue;
    }
    found_table = true;

    // Fetch table.
    fbl::Array<uint8_t> table_data;
    if (zx_status_t status = FetchTable(*dev.channel(), table, &table_data); status != ZX_OK) {
      fprintf(stderr, "Failed to read table '%s': %s\n", SignatureToString(table.name).c_str(),
              zx_status_get_string(status));
      return 1;
    }

    // Print data.
    if (args.dump_raw) {
      PrintRaw(table_data);
    } else {
      PrintHex(SignatureToString(table.name).c_str(), table_data);
    }
  }

  // Print an error if we didn't find the user's requested table.
  if (args.table.has_value() && !found_table) {
    fprintf(stderr, "Table '%s' not found.\n", args.table->c_str());
    return 1;
  }

  return 0;
}

int Main(int argc, const char** argv) {
  Args args;
  if (!ParseArgs(fbl::Span<const char*>(argv, argc), &args)) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (args.show_help) {
    PrintUsage(argv[0]);
    return 0;
  }

  return AcpiDump(args) == ZX_OK ? 0 : 1;
}

}  // namespace acpidump
