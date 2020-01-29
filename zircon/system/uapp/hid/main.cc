// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/event.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <optional>
#include <utility>
#include <vector>

#include <fbl/algorithm.h>

// defined in report.cpp
void print_report_descriptor(const uint8_t* rpt_desc, size_t desc_len);

#define DEV_INPUT "/dev/class/input"

static bool verbose = false;
#define xprintf(fmt...) \
  do {                  \
    if (verbose)        \
      printf(fmt);      \
  } while (0)

enum class Command { read, readall, get, set, parse };

void usage(void) {
  printf("usage: hid [-v] <command> [<args>]\n\n");
  printf("  commands:\n");
  printf("    read [<devpath> [num reads]]\n");
  printf("    get <devpath> <in|out|feature> <id>\n");
  printf("    set <devpath> <in|out|feature> <id> [0xXX *]\n");
  printf("    parse <devpath>\n");
}

constexpr size_t kDevPathSize = 128;

struct input_args_t {
  Command command;

  std::optional<llcpp::fuchsia::hardware::input::Device::SyncClient> sync_client;

  char devpath[kDevPathSize];
  size_t num_reads;

  llcpp::fuchsia::hardware::input::ReportType report_type;
  uint8_t report_id;

  const char** data;
  size_t data_size;
};

static thrd_t input_poll_thread;

static mtx_t print_lock = MTX_INIT;
#define lprintf(fmt...)      \
  do {                       \
    mtx_lock(&print_lock);   \
    printf(fmt);             \
    mtx_unlock(&print_lock); \
  } while (0)

static void print_hex(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printf("%02x ", buf[i]);
    if (i % 16 == 15)
      printf("\n");
  }
  printf("\n");
}

static zx_status_t parse_uint_arg(const char* arg, uint32_t min, uint32_t max, uint32_t* out_val) {
  if ((arg == NULL) || (out_val == NULL)) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool is_hex = (arg[0] == '0') && (arg[1] == 'x');
  if (sscanf(arg, is_hex ? "%x" : "%u", out_val) != 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((*out_val < min) || (*out_val > max)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

static zx_status_t parse_input_report_type(const char* arg,
                                           llcpp::fuchsia::hardware::input::ReportType* out_type) {
  if ((arg == NULL) || (out_type == NULL)) {
    return ZX_ERR_INVALID_ARGS;
  }

  static const struct {
    const char* name;
    llcpp::fuchsia::hardware::input::ReportType type;
  } LUT[] = {
      {.name = "in", .type = llcpp::fuchsia::hardware::input::ReportType::INPUT},
      {.name = "out", .type = llcpp::fuchsia::hardware::input::ReportType::OUTPUT},
      {.name = "feature", .type = llcpp::fuchsia::hardware::input::ReportType::FEATURE},
  };

  for (size_t i = 0; i < fbl::count_of(LUT); ++i) {
    if (!strcasecmp(arg, LUT[i].name)) {
      *out_type = LUT[i].type;
      return ZX_OK;
    }
  }

  return ZX_ERR_INVALID_ARGS;
}

static zx_status_t print_hid_protocol(input_args_t* args) {
  auto result = args->sync_client->GetBootProtocol();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get protocol from %s (status=%d)\n", args->devpath, result.status());
  } else {
    lprintf("hid: %s proto=%d\n", args->devpath, static_cast<uint32_t>(result->protocol));
  }
  return ZX_OK;
}

static zx_status_t get_report_desc_len(input_args_t* args, size_t* report_desc_len) {
  auto result = args->sync_client->GetReportDescSize();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get report descriptor length from %s (status=%d)\n", args->devpath,
            result.status());
  } else {
    *report_desc_len = result->size;
    lprintf("hid: %s report descriptor len=%zu\n", args->devpath, *report_desc_len);
  }
  return ZX_OK;
}

static zx_status_t print_report_desc(input_args_t* args, size_t report_desc_len) {
  auto result = args->sync_client->GetReportDesc();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get report descriptor from %s (status=%d)\n", args->devpath,
            result.status());
    return result.status();
  }

  if (result->desc.count() != report_desc_len) {
    lprintf("hid: got unexpected length on report descriptor: %zu versus %zu\n",
            result->desc.count(), report_desc_len);
    return ZX_ERR_BAD_STATE;
  }

  mtx_lock(&print_lock);
  printf("hid: %s report descriptor:\n", args->devpath);
  if (verbose) {
    print_hex(result->desc.data(), result->desc.count());
  }
  print_report_descriptor(result->desc.data(), result->desc.count());
  mtx_unlock(&print_lock);
  return ZX_OK;
}

static zx_status_t print_device_ids(input_args_t* args) {
  auto result = args->sync_client->GetDeviceIds();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get device ids from %s (status=%d)\n", args->devpath, result.status());
    return result.status();
  }

  mtx_lock(&print_lock);
  printf("hid device ids:\n");
  printf("  vendor_id:  0x%08x\n", result->ids.vendor_id);
  printf("  product_id: 0x%08x\n", result->ids.product_id);
  printf("  version:    0x%08x\n", result->ids.version);
  mtx_unlock(&print_lock);

  return ZX_OK;
}

static zx_status_t get_num_reports(input_args_t* args, size_t* num_reports) {
  auto result = args->sync_client->GetNumReports();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get number of reports from %s (status=%d)\n", args->devpath,
            result.status());
  } else {
    *num_reports = result->count;
    lprintf("hid: %s num reports: %zu\n", args->devpath, *num_reports);
  }
  return ZX_OK;
}

static zx_status_t print_report_ids(input_args_t* args, size_t num_reports) {
  auto result = args->sync_client->GetReportIds();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get report ids from %s (status=%d)\n", args->devpath, result.status());
    return ZX_OK;
  }
  if (result->ids.count() != num_reports) {
    lprintf("hid: got unexpected number of reports: %zu versus %zu\n", result->ids.count(),
            num_reports);
    return ZX_ERR_BAD_STATE;
  }

  mtx_lock(&print_lock);
  printf("hid: %s report ids...\n", args->devpath);
  for (size_t i = 0; i < num_reports; i++) {
    static const struct {
      llcpp::fuchsia::hardware::input::ReportType type;
      const char* tag;
    } TYPES[] = {
        {.type = llcpp::fuchsia::hardware::input::ReportType::INPUT, .tag = "Input"},
        {.type = llcpp::fuchsia::hardware::input::ReportType::OUTPUT, .tag = "Output"},
        {.type = llcpp::fuchsia::hardware::input::ReportType::FEATURE, .tag = "Feature"},
    };

    bool found = false;
    for (size_t j = 0; j < fbl::count_of(TYPES); ++j) {
      auto res = args->sync_client->GetReportSize(TYPES[j].type, result->ids[i]);
      if (res.status() == ZX_OK && res->status == ZX_OK) {
        printf("  ID 0x%02x : TYPE %7s : SIZE %u bytes\n", result->ids[i], TYPES[j].tag, res->size);
        found = true;
      }
    }

    if (!found) {
      printf("  hid: failed to find any report sizes for report id 0x%02x's (dev %s)\n",
             result->ids[i], args->devpath);
    }
  }

  mtx_unlock(&print_lock);
  return ZX_OK;
}

static zx_status_t get_max_report_len(input_args_t* args, uint16_t* max_report_len) {
  auto result = args->sync_client->GetMaxInputReportSize();
  if (result.status() != ZX_OK) {
    lprintf("hid: could not get max report size from %s (status=%d)\n", args->devpath,
            result.status());
  } else {
    lprintf("hid: %s maxreport=%u\n", args->devpath, result->size);
  }
  if (max_report_len) {
    *max_report_len = result->size;
  }
  return ZX_OK;
}

#define TRY(fn)              \
  do {                       \
    zx_status_t status = fn; \
    if (status != ZX_OK)     \
      return status;         \
  } while (0)

static zx_status_t hid_status(input_args_t* args, uint16_t* max_report_len) {
  size_t num_reports;

  TRY(print_hid_protocol(args));
  TRY(get_num_reports(args, &num_reports));
  TRY(print_report_ids(args, num_reports));
  TRY(get_max_report_len(args, max_report_len));
  return ZX_OK;
}

static zx_status_t parse_rpt_descriptor(input_args_t* args) {
  size_t report_desc_len;
  TRY(get_report_desc_len(args, &report_desc_len));
  TRY(print_report_desc(args, report_desc_len));
  return ZX_OK;
}

#undef TRY

static zx_status_t hid_input_read_report(input_args_t* args, const zx::event& report_event,
                                         size_t report_size, uint8_t* report_data,
                                         size_t* returned_size) {
  while (true) {
    auto result = args->sync_client->ReadReport();
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->status == ZX_ERR_SHOULD_WAIT) {
      report_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
      continue;
    }
    if (result->data.count() > report_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    *returned_size = result->data.count();
    memcpy(report_data, result->data.data(), result->data.count());

    return ZX_OK;
  }
}

static int hid_read_reports(input_args_t* args) {
  uint16_t max_report_len = 0;
  ssize_t rc = hid_status(args, &max_report_len);
  if (rc < 0) {
    return static_cast<int>(rc);
  }

  zx_status_t status;
  zx::event report_event;
  auto result = args->sync_client->GetReportsEvent();
  if ((result.status() != ZX_OK) || (result->status != ZX_OK)) {
    mtx_lock(&print_lock);
    printf("read returned error: (call_status=%d) (status=%d)\n", result.status(), result->status);
    mtx_unlock(&print_lock);
    return ZX_ERR_INTERNAL;
  }
  report_event = std::move(result->event);

  // Add 1 to the max report length to make room for a Report ID.
  max_report_len++;
  std::vector<uint8_t> report(max_report_len);
  for (uint32_t i = 0; i < args->num_reads; i++) {
    size_t returned_size;
    status =
        hid_input_read_report(args, report_event, report.size(), report.data(), &returned_size);
    if (status != ZX_OK) {
      mtx_lock(&print_lock);
      printf("hid_input_read_report returned %d\n", status);
      mtx_unlock(&print_lock);
      break;
    }

    mtx_lock(&print_lock);
    printf("read returned %ld bytes\n", returned_size);
    printf("hid: input from %s\n", args->devpath);
    print_hex(report.data(), returned_size);
    mtx_unlock(&print_lock);
  }

  lprintf("hid: closing %s\n", args->devpath);
  return ZX_OK;
}

static int hid_input_thread(void* arg) {
  input_args_t* args = (input_args_t*)arg;
  lprintf("hid: input thread started for %s\n", args->devpath);

  zx_status_t status = hid_read_reports(args);

  delete args;
  return status;
}

static zx_status_t hid_input_device_added(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  int fd = openat(dirfd, fn, O_RDONLY);
  if (fd < 0) {
    return ZX_OK;
  }

  input_args_t* args = new input_args_t;

  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd, chan.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  args->sync_client = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(chan));

  // TODO: support setting num_reads across all devices. requires a way to
  // signal shutdown to all input threads.
  args->num_reads = ULONG_MAX;
  snprintf(args->devpath, kDevPathSize, "%s", fn);

  thrd_t t;
  int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->devpath);
  if (ret != thrd_success) {
    printf("hid: input thread %s did not start (error=%d)\n", args->devpath, ret);
    close(fd);
    return thrd_status_to_zx_status(ret);
  }
  thrd_detach(t);
  return ZX_OK;
}

static int hid_input_devices_poll_thread(void* arg) {
  int dirfd = open(DEV_INPUT, O_DIRECTORY | O_RDONLY);
  if (dirfd < 0) {
    printf("hid: error opening %s\n", DEV_INPUT);
    return ZX_ERR_INTERNAL;
  }
  fdio_watch_directory(dirfd, hid_input_device_added, ZX_TIME_INFINITE, NULL);
  close(dirfd);
  return -1;
}

int readall_reports() {
  int ret = thrd_create_with_name(&input_poll_thread, hid_input_devices_poll_thread, NULL,
                                  "hid-inputdev-poll");
  if (ret != thrd_success) {
    return -1;
  }

  thrd_join(input_poll_thread, NULL);
  return 0;
}

// Get a single report from the device with a given report id and then print it.
int get_report(input_args_t* args) {
  auto result = args->sync_client->GetReport(args->report_type, args->report_id);
  if (result.status() != ZX_OK || result->status != ZX_OK) {
    printf("hid: could not get report: %d, %d\n", result.status(), result->status);
    return -1;
  }

  printf("hid: got %zu bytes\n", result->report.count());
  print_hex(result->report.data(), result->report.count());
  return 0;
}

int set_report(input_args_t* args) {
  xprintf("hid: setting report size for id=0x%02x\n", args->report_id);

  auto result = args->sync_client->GetReportSize(args->report_type, args->report_id);
  if (result.status() != ZX_OK || result->status != ZX_OK) {
    printf("hid: could not get report (id 0x%02x type %u) size from %s (status=%d, %d)\n",
           args->report_id, static_cast<uint8_t>(args->report_type), args->devpath, result.status(),
           result->status);
    return -1;
  }

  xprintf("hid: report size=%u, tx payload size=%lu\n", result->size, args->data_size);

  std::unique_ptr<uint8_t[]> report(new uint8_t[args->data_size]);
  for (size_t i = 0; i < args->data_size; i++) {
    uint32_t tmp;
    zx_status_t res = parse_uint_arg(args->data[i], 0, 255, &tmp);
    if (res != ZX_OK) {
      printf("Failed to parse payload byte \"%s\" (res = %d)\n", args->data[i], res);
      return res;
    }

    report[i] = static_cast<uint8_t>(tmp);
  }

  fidl::VectorView<uint8_t> report_view = fidl::VectorView<uint8_t>(report.get(), args->data_size);
  auto res = args->sync_client->SetReport(args->report_type, args->report_id, report_view);
  if (res.status() != ZX_OK || res->status != ZX_OK) {
    printf("hid: could not set report: %d, %d\n", res.status(), res->status);
    return -1;
  }

  printf("hid: success\n");
  return 0;
}

zx_status_t parse_input_args(int argc, const char** argv, input_args_t* args) {
  zx_status_t status;
  // Move past the first arg which is just the binary.
  argc--;
  argv++;

  if (argc == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!strcmp("-v", argv[0])) {
    verbose = true;
    argc--;
    argv++;
  }

  if (argc == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Parse the command name.
  if (!strcmp("read", argv[0])) {
    if (argc == 1) {
      args->command = Command::readall;
      return ZX_OK;
    }
    args->command = Command::read;
  } else if (!strcmp("get", argv[0])) {
    args->command = Command::get;
  } else if (!strcmp("set", argv[0])) {
    args->command = Command::set;
  } else if (!strcmp("parse", argv[0])) {
    args->command = Command::parse;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  if (argc < 2) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Parse <devpath>
  int fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("could not open %s: %d\n", argv[0], errno);
    return ZX_ERR_INTERNAL;
  }
  zx::channel chan;
  status = fdio_get_service_handle(fd, chan.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  args->sync_client = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(chan));
  snprintf(args->devpath, kDevPathSize, "%s", argv[1]);

  if (args->command == Command::parse) {
    if (argc > 2) {
      return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  }

  // Parse Read arguments.
  if (args->command == Command::read) {
    if (argc == 3) {
      uint32_t tmp = std::numeric_limits<uint32_t>::max();
      status = parse_uint_arg(argv[2], 0, std::numeric_limits<uint32_t>::max(), &tmp);
      if (status != ZX_OK) {
        return status;
      }
      args->num_reads = tmp;
    } else if (argc == 2) {
      args->num_reads = std::numeric_limits<uint32_t>::max();
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  }

  if (argc < 4) {
    return ZX_ERR_INTERNAL;
  }

  // Parse ReportType argument.
  llcpp::fuchsia::hardware::input::ReportType report_type;
  status = parse_input_report_type(argv[2], &report_type);
  if (status != ZX_OK) {
    return status;
  }
  args->report_type = report_type;

  // Parse Report id.
  uint32_t report_id = 255;
  status = parse_uint_arg(argv[3], 0, 255, &report_id);
  if (status != ZX_OK) {
    return status;
  }
  args->report_id = static_cast<uint8_t>(report_id);

  if ((args->command == Command::get) && (argc > 4)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((args->command == Command::set) && (argc > 4)) {
    args->data_size = argc - 4;
    args->data = &argv[4];
  }

  return ZX_OK;
}

int main(int argc, const char** argv) {
  input_args_t args = {};
  zx_status_t status = parse_input_args(argc, argv, &args);
  if (status != ZX_OK) {
    usage();
    return 1;
  }

  if (args.command == Command::parse) {
    return parse_rpt_descriptor(&args);
  } else if (args.command == Command::get) {
    return get_report(&args);
  } else if (args.command == Command::set) {
    return set_report(&args);
  } else if (args.command == Command::read) {
    return hid_read_reports(&args);
  } else if (args.command == Command::readall) {
    return readall_reports();
  }

  return 1;
}
