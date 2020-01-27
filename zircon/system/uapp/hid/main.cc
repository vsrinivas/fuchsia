// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
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

void usage(void) {
  printf("usage: hid [-v] <command> [<args>]\n\n");
  printf("  commands:\n");
  printf("    read [<devpath> [num reads]]\n");
  printf("    get <devpath> <in|out|feature> <id>\n");
  printf("    set <devpath> <in|out|feature> <id> [0xXX *]\n");
  printf("    parse <devpath>\n");
}

typedef struct input_args {
  fbl::unique_fd fd;
  char name[128];
  unsigned long int num_reads;
} input_args_t;

static thrd_t input_poll_thread;

static mtx_t print_lock = MTX_INIT;
#define lprintf(fmt...)      \
  do {                       \
    mtx_lock(&print_lock);   \
    printf(fmt);             \
    mtx_unlock(&print_lock); \
  } while (0)

static void print_hex(uint8_t* buf, size_t len) {
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
                                           fuchsia_hardware_input_ReportType* out_type) {
  if ((arg == NULL) || (out_type == NULL)) {
    return ZX_ERR_INVALID_ARGS;
  }

  static const struct {
    const char* name;
    fuchsia_hardware_input_ReportType type;
  } LUT[] = {
      {.name = "in", .type = fuchsia_hardware_input_ReportType_INPUT},
      {.name = "out", .type = fuchsia_hardware_input_ReportType_OUTPUT},
      {.name = "feature", .type = fuchsia_hardware_input_ReportType_FEATURE},
  };

  for (size_t i = 0; i < fbl::count_of(LUT); ++i) {
    if (!strcasecmp(arg, LUT[i].name)) {
      *out_type = LUT[i].type;
      return ZX_OK;
    }
  }

  return ZX_ERR_INVALID_ARGS;
}

static zx_status_t parse_set_get_report_args(int argc, const char** argv, uint8_t* out_id,
                                             fuchsia_hardware_input_ReportType* out_type) {
  if ((argc < 3) || (argv == NULL) || (out_id == NULL) || (out_type == NULL)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t res;
  uint32_t tmp;
  res = parse_uint_arg(argv[2], 0, 255, &tmp);
  if (res != ZX_OK) {
    return res;
  }

  *out_id = static_cast<uint8_t>(tmp);

  return parse_input_report_type(argv[1], out_type);
}

static zx_status_t get_hid_protocol(const fzl::FdioCaller& caller, const char* name) {
  uint32_t proto;
  zx_status_t status =
      fuchsia_hardware_input_DeviceGetBootProtocol(caller.borrow_channel(), &proto);
  if (status != ZX_OK) {
    lprintf("hid: could not get protocol from %s (status=%d)\n", name, status);
  } else {
    lprintf("hid: %s proto=%d\n", name, proto);
  }
  return status;
}

static zx_status_t get_report_desc_len(const fzl::FdioCaller& caller, const char* name,
                                       size_t* report_desc_len) {
  uint16_t len;
  zx_status_t status =
      fuchsia_hardware_input_DeviceGetReportDescSize(caller.borrow_channel(), &len);
  if (status != ZX_OK) {
    lprintf("hid: could not get report descriptor length from %s (status=%d)\n", name, status);
  } else {
    *report_desc_len = len;
    lprintf("hid: %s report descriptor len=%zu\n", name, *report_desc_len);
  }
  return status;
}

static zx_status_t get_report_desc(const fzl::FdioCaller& caller, const char* name,
                                   size_t report_desc_len) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[report_desc_len]);

  size_t actual;
  zx_status_t status = fuchsia_hardware_input_DeviceGetReportDesc(
      caller.borrow_channel(), buf.get(), report_desc_len, &actual);
  if (status != ZX_OK) {
    lprintf("hid: could not get report descriptor from %s (status=%d)\n", name, status);
    return status;
  }
  if (actual != report_desc_len) {
    lprintf("hid: got unexpected length on report descriptor: %zu versus %zu\n", actual,
            report_desc_len);
    return ZX_ERR_BAD_STATE;
  }
  mtx_lock(&print_lock);
  printf("hid: %s report descriptor:\n", name);
  if (verbose) {
    print_hex(buf.get(), report_desc_len);
  }
  print_report_descriptor(buf.get(), report_desc_len);
  mtx_unlock(&print_lock);
  return status;
}

static zx_status_t print_device_ids(const fzl::FdioCaller& caller, const char* name) {
  fuchsia_hardware_input_DeviceIds ids = {};
  zx_status_t status = fuchsia_hardware_input_DeviceGetDeviceIds(caller.borrow_channel(), &ids);
  if (status != ZX_OK) {
    lprintf("hid: could not get device ids from %s (status=%d)\n", name, status);
    return status;
  }

  mtx_lock(&print_lock);
  printf("hid device ids:\n");
  printf("  vendor_id:  0x%08x\n", ids.vendor_id);
  printf("  product_id: 0x%08x\n", ids.product_id);
  printf("  version:    0x%08x\n", ids.version);
  mtx_unlock(&print_lock);

  return ZX_OK;
}

static zx_status_t get_num_reports(const fzl::FdioCaller& caller, const char* name,
                                   size_t* num_reports) {
  uint16_t count;
  zx_status_t status = fuchsia_hardware_input_DeviceGetNumReports(caller.borrow_channel(), &count);
  if (status != ZX_OK) {
    lprintf("hid: could not get number of reports from %s (status=%d)\n", name, status);
  } else {
    *num_reports = count;
    lprintf("hid: %s num reports: %zu\n", name, *num_reports);
  }
  return status;
}

static zx_status_t get_report_ids(const fzl::FdioCaller& caller, const char* name,
                                  size_t num_reports) {
  std::unique_ptr<uint8_t[]> ids(new uint8_t[num_reports]);

  size_t actual;
  zx_status_t status = fuchsia_hardware_input_DeviceGetReportIds(caller.borrow_channel(), ids.get(),
                                                                 num_reports, &actual);
  if (status != ZX_OK) {
    lprintf("hid: could not get report ids from %s (status=%d)\n", name, status);
    return status;
  }
  if (actual != num_reports) {
    lprintf("hid: got unexpected number of reports: %zu versus %zu\n", actual, num_reports);
    return ZX_ERR_BAD_STATE;
  }

  mtx_lock(&print_lock);
  printf("hid: %s report ids...\n", name);
  for (size_t i = 0; i < num_reports; i++) {
    static const struct {
      fuchsia_hardware_input_ReportType type;
      const char* tag;
    } TYPES[] = {
        {.type = fuchsia_hardware_input_ReportType_INPUT, .tag = "Input"},
        {.type = fuchsia_hardware_input_ReportType_OUTPUT, .tag = "Output"},
        {.type = fuchsia_hardware_input_ReportType_FEATURE, .tag = "Feature"},
    };

    bool found = false;
    for (size_t j = 0; j < fbl::count_of(TYPES); ++j) {
      uint16_t size;

      zx_status_t call_status;
      status = fuchsia_hardware_input_DeviceGetReportSize(caller.borrow_channel(), TYPES[j].type,
                                                          ids[i], &call_status, &size);
      if (status == ZX_OK && call_status == ZX_OK) {
        printf("  ID 0x%02x : TYPE %7s : SIZE %u bytes\n", ids[i], TYPES[j].tag, size);
        found = true;
      }
    }

    if (!found) {
      printf("  hid: failed to find any report sizes for report id 0x%02x's (dev %s)\n", ids[i],
             name);
    }
  }

  mtx_unlock(&print_lock);
  return status;
}

static zx_status_t get_max_report_len(const fzl::FdioCaller& caller, const char* name,
                                      uint16_t* max_report_len) {
  uint16_t tmp;
  if (max_report_len == NULL) {
    max_report_len = &tmp;
  }
  zx_status_t status =
      fuchsia_hardware_input_DeviceGetMaxInputReportSize(caller.borrow_channel(), max_report_len);
  if (status != ZX_OK) {
    lprintf("hid: could not get max report size from %s (status=%d)\n", name, status);
  } else {
    lprintf("hid: %s maxreport=%u\n", name, *max_report_len);
  }
  return status;
}

#define TRY(fn)              \
  do {                       \
    zx_status_t status = fn; \
    if (status != ZX_OK)     \
      return status;         \
  } while (0)

static zx_status_t hid_status(const fzl::FdioCaller& caller, const char* name,
                              uint16_t* max_report_len) {
  size_t num_reports;

  TRY(get_hid_protocol(caller, name));
  TRY(get_num_reports(caller, name, &num_reports));
  TRY(get_report_ids(caller, name, num_reports));
  TRY(get_max_report_len(caller, name, max_report_len));
  return ZX_OK;
}

static zx_status_t parse_rpt_descriptor(const fzl::FdioCaller& caller, const char* name) {
  size_t report_desc_len;
  TRY(get_report_desc_len(caller, "", &report_desc_len));
  TRY(get_report_desc(caller, "", report_desc_len));
  return ZX_OK;
}

#undef TRY

static zx_status_t hid_input_read_report(zx_handle_t channel, const zx::event& report_event,
                                         uint8_t* report_data, size_t report_size,
                                         size_t* returned_size) {
  zx_time_t time;
  zx_status_t status;
  while (true) {
    zx_status_t call_status = fuchsia_hardware_input_DeviceReadReport(
        channel, &status, report_data, report_size, returned_size, &time);
    if (call_status != ZX_OK) {
      return call_status;
    }
    if (status == ZX_ERR_SHOULD_WAIT) {
      report_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
      continue;
    }
    return status;
  }
}

static int hid_input_thread(void* arg) {
  input_args_t* args = (input_args_t*)arg;
  lprintf("hid: input thread started for %s\n", args->name);

  fzl::FdioCaller caller(std::move(args->fd));

  uint16_t max_report_len = 0;
  ssize_t rc = hid_status(caller, args->name, &max_report_len);
  if (rc < 0) {
    return static_cast<int>(rc);
  }

  zx_status_t status;
  zx::event report_event;
  zx_status_t call_status = fuchsia_hardware_input_DeviceGetReportsEvent(
      caller.borrow_channel(), &status, report_event.reset_and_get_address());
  if ((call_status != ZX_OK) || (status != ZX_OK)) {
    mtx_lock(&print_lock);
    printf("read returned error: (call_status=%d) (status=%d)\n", call_status, status);
    mtx_unlock(&print_lock);
    return ZX_ERR_INTERNAL;
  }

  // Add 1 to the max report length to make room for a Report ID.
  max_report_len++;
  std::vector<uint8_t> report(max_report_len);
  for (uint32_t i = 0; i < args->num_reads; i++) {
    size_t returned_size;
    status = hid_input_read_report(caller.borrow_channel(), report_event, report.data(),
                                   report.size(), &returned_size);
    if (status != ZX_OK) {
      mtx_lock(&print_lock);
      printf("hid_input_read_report returned %d\n", status);
      mtx_unlock(&print_lock);
      break;
    }

    mtx_lock(&print_lock);
    printf("read returned %ld\n bytes", returned_size);
    printf("hid: input from %s\n", args->name);
    print_hex(report.data(), returned_size);
    mtx_unlock(&print_lock);
  }

  lprintf("hid: closing %s\n", args->name);
  delete args;
  return ZX_OK;
}

static zx_status_t hid_input_device_added(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  int fd = openat(dirfd, fn, O_RDONLY);
  if (fd < 0) {
    return ZX_OK;
  }

  input_args_t* args = new input_args{};
  args->fd = fbl::unique_fd(fd);
  // TODO: support setting num_reads across all devices. requires a way to
  // signal shutdown to all input threads.
  args->num_reads = ULONG_MAX;
  thrd_t t;
  snprintf(args->name, sizeof(args->name), "hid-input-%s", fn);
  int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->name);
  if (ret != thrd_success) {
    printf("hid: input thread %s did not start (error=%d)\n", args->name, ret);
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

int read_reports(int argc, const char** argv) {
  argc--;
  argv++;
  if (argc < 1) {
    usage();
    return 0;
  }

  uint32_t tmp = 0xffffffff;
  if (argc > 1) {
    zx_status_t res = parse_uint_arg(argv[1], 0, 0xffffffff, &tmp);
    if (res != ZX_OK) {
      printf("Failed to parse <num reads> (res %d)\n", res);
      usage();
      return 0;
    }
  }

  int fd = open(argv[0], O_RDWR);
  if (fd < 0) {
    printf("could not open %s: %d\n", argv[0], errno);
    return -1;
  }

  input_args_t* args = new input_args_t{};
  args->fd = fbl::unique_fd(fd);
  args->num_reads = tmp;

  strlcpy(args->name, argv[0], sizeof(args->name));
  thrd_t t;
  int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->name);
  if (ret != thrd_success) {
    printf("hid: input thread %s did not start (error=%d)\n", args->name, ret);
    delete args;
    return -1;
  }
  thrd_join(t, NULL);
  return 0;
}

int readall_reports(int argc, const char** argv) {
  int ret = thrd_create_with_name(&input_poll_thread, hid_input_devices_poll_thread, NULL,
                                  "hid-inputdev-poll");
  if (ret != thrd_success) {
    return -1;
  }

  thrd_join(input_poll_thread, NULL);
  return 0;
}

int get_report(int argc, const char** argv) {
  argc--;
  argv++;
  if (argc < 3) {
    usage();
    return 0;
  }

  uint8_t id;
  fuchsia_hardware_input_ReportType type;
  zx_status_t res = parse_set_get_report_args(argc, argv, &id, &type);
  if (res != ZX_OK) {
    printf("Failed to parse type/id for get report operation (res %d)\n", res);
    usage();
    return 0;
  }

  int fd = open(argv[0], O_RDWR);
  if (fd < 0) {
    printf("could not open %s: %d\n", argv[0], errno);
    return -1;
  }
  fzl::FdioCaller caller{fbl::unique_fd(fd)};

  xprintf("hid: getting report size for id=0x%02x type=%u\n", id, type);

  uint16_t size;
  zx_status_t call_status;
  res = fuchsia_hardware_input_DeviceGetReportSize(caller.borrow_channel(), type, id, &call_status,
                                                   &size);
  if (res != ZX_OK || call_status != ZX_OK) {
    printf("hid: could not get report (id 0x%02x type %u) size from %s (status=%d, %d)\n", id, type,
           argv[0], res, call_status);
    return static_cast<int>(-1);
  }
  xprintf("hid: report size=%u\n", size);

  // TODO(johngro) : Come up with a better policy than this...  While devices
  // are *supposed* to only deliver a report descriptor's computed size, in
  // practice they frequently seem to deliver number of bytes either greater
  // or fewer than the number of bytes originally requested.  For example...
  //
  // ++ Sometimes a device is expected to deliver a Report ID byte along with
  //    the payload contents, but does not do so.
  // ++ Sometimes it is unclear whether or not a device needs to deliver a
  //    Report ID byte at all since there is only one report listed (and,
  //    sometimes the device delivers that ID, and sometimes it chooses not
  //    to).
  // ++ Sometimes no bytes at all are returned for a report (this seems to
  //    be relatively common for input reports)
  // ++ Sometimes the number of bytes returned has basically nothing to do
  //    with the expected size of the report (this seems to be relatively
  //    common for vendor feature reports).
  //
  // Because of this uncertainty, we currently just provide a worst-case 4KB
  // buffer to read into, and report the number of bytes which came back along
  // with the expected size of the raw report.
  size_t bufsz = 4u << 10;
  size_t actual;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[bufsz]);
  res = fuchsia_hardware_input_DeviceGetReport(caller.borrow_channel(), type, id, &call_status,
                                               buf.get(), bufsz, &actual);
  if (res != ZX_OK || call_status != ZX_OK) {
    printf("hid: could not get report: %d, %d\n", res, call_status);
    return -1;
  }

  printf("hid: got %zu bytes (raw report size %u)\n", actual, size);
  print_hex(buf.get(), actual);
  return 0;
}

int set_report(int argc, const char** argv) {
  argc--;
  argv++;
  if (argc < 4) {
    usage();
    return 0;
  }

  uint8_t id;
  fuchsia_hardware_input_ReportType type;
  zx_status_t res = parse_set_get_report_args(argc, argv, &id, &type);
  if (res != ZX_OK) {
    printf("Failed to parse type/id for get report operation (res %d)\n", res);
    usage();
    return 0;
  }

  xprintf("hid: setting report size for id=0x%02x type=%u\n", id, type);

  int fd = open(argv[0], O_RDWR);
  if (fd < 0) {
    printf("could not open %s: %d\n", argv[0], errno);
    return -1;
  }
  fzl::FdioCaller caller{fbl::unique_fd(fd)};

  // If the set/get report args parsed, then we must have at least 3 arguments.
  ZX_DEBUG_ASSERT(argc >= 3);
  uint16_t payload_size = static_cast<uint16_t>(argc - 3);

  uint16_t size;
  zx_status_t call_status;
  res = fuchsia_hardware_input_DeviceGetReportSize(caller.borrow_channel(), type, id, &call_status,
                                                   &size);
  if (res != ZX_OK || call_status != ZX_OK) {
    printf("hid: could not get report (id 0x%02x type %u) size from %s (status=%d, %d)\n", id, type,
           argv[0], res, call_status);
    return -1;
  }

  xprintf("hid: report size=%u, tx payload size=%u\n", size, payload_size);

  std::unique_ptr<uint8_t[]> report(new uint8_t[payload_size]);
  for (int i = 0; i < payload_size; i++) {
    uint32_t tmp;
    zx_status_t res = parse_uint_arg(argv[i + 3], 0, 255, &tmp);
    if (res != ZX_OK) {
      printf("Failed to parse payload byte \"%s\" (res = %d)\n", argv[i + 3], res);
      return res;
    }

    report[i] = static_cast<uint8_t>(tmp);
  }

  res = fuchsia_hardware_input_DeviceSetReport(caller.borrow_channel(), type, id, report.get(),
                                               payload_size, &call_status);
  if (res != ZX_OK || call_status != ZX_OK) {
    printf("hid: could not set report: %d, %d\n", res, call_status);
    return -1;
  }

  printf("hid: success\n");
  return 0;
}

int parse(int argc, const char** argv) {
  argc--;
  argv++;
  if (argc < 1) {
    usage();
    return 0;
  }

  int fd = open(argv[0], O_RDWR);
  if (fd < 0) {
    printf("could not open %s: %d\n", argv[0], errno);
    return -1;
  }

  fzl::FdioCaller caller{fbl::unique_fd(fd)};
  zx_status_t status = print_device_ids(caller, argv[0]);
  if (status != ZX_OK) {
    return static_cast<int>(status);
  }

  status = parse_rpt_descriptor(caller, argv[0]);
  return static_cast<int>(status);
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  argc--;
  argv++;
  if (!strcmp("-v", argv[0])) {
    verbose = true;
    argc--;
    argv++;
  }
  if (!strcmp("read", argv[0])) {
    if (argc > 1) {
      return read_reports(argc, argv);
    } else {
      return readall_reports(argc, argv);
    }
  }

  if (!strcmp("get", argv[0])) {
    return get_report(argc, argv);
  }

  if (!strcmp("set", argv[0])) {
    return set_report(argc, argv);
  }

  if (!strcmp("parse", argv[0])) {
    return parse(argc, argv);
  }

  usage();
  return 0;
}
