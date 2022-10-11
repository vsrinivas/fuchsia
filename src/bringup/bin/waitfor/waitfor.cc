// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/lib/uuid/uuid.h"

void usage() {
  fprintf(stderr,
          "usage: waitfor <expr>+        wait for devices to be published\n"
          "\n"
          "expr:  class=<name>           device class <name>   (required)\n"
          "\n"
          "       topo=<path>            topological path contains <path>\n"
          "       part.guid=<guid>       block device GUID matches <guid>\n"
          "       part.type.guid=<guid>  partition type GUID matches <guid>\n"
          "       part.name=<name>       partition name matches <name>\n"
          "\n"
          "       timeout=<msec>         fail if no match after <msec> milliseconds\n"
          "       print                  write name of matching devices to stdout\n"
          "       forever                don't stop after the first match\n"
          "                              also don't fail on timeout after first match\n"
          "       verbose                print debug chatter to stderr\n"
          "\n"
          "example: waitfor class=block part.name=system print\n");
}

struct App;

class Rule {
 public:
  using Func = zx_status_t (*)(const App& app, const char* arg, int fd);

  Rule(const char* arg, Func func) : func_(func), arg_(arg) {}

  zx_status_t CallWithFd(const App& app, int fd) const { return func_(app, arg_, fd); }

 private:
  Func func_;
  const char* arg_;
};

struct App {
  bool matched;

  const bool verbose;
  const bool print;
  const bool forever;
  const char* devclass;
  const std::vector<Rule> rules;
};

zx_status_t watchcb(int dirfd, int event, const char* fn, void* cookie) {
  if (std::string_view{fn} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  App& app = *static_cast<App*>(cookie);
  if (app.verbose) {
    fprintf(stderr, "waitfor: device='/dev/class/%s/%s'\n", app.devclass, fn);
  }
  const fbl::unique_fd fd(openat(dirfd, fn, O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "waitfor: warning: failed to open '/dev/class/%s/%s': %s\n", app.devclass, fn,
            strerror(errno));
    return ZX_OK;
  }

  for (const Rule& r : app.rules) {
    switch (const zx_status_t status = r.CallWithFd(app, fd.get()); status) {
      case ZX_OK:
        // rule matched
        continue;
      case ZX_ERR_NEXT:
        // rule did not match
        return ZX_OK;
      default:
        // fatal error
        return status;
    }
  }

  app.matched = true;

  if (app.print) {
    printf("/dev/class/%s/%s\n", app.devclass, fn);
  }

  if (app.forever) {
    return ZX_OK;
  }
  return ZX_ERR_STOP;
}

// Expression evaluators return OK on match, NEXT on no-match
// any other error is fatal

zx_status_t expr_topo(const App& app, const char* arg, int fd) {
  const fdio_cpp::UnownedFdioCaller caller(fd);
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_device::Controller>())->GetTopologicalPath();
  if (!result.ok()) {
    fprintf(stderr, "waitfor: warning: cannot request topological path: %s\n",
            result.FormatDescription().c_str());
    return result.status();
  }
  const auto* res = result.Unwrap();
  if (res->is_error()) {
    const zx_status_t status = res->error_value();
    fprintf(stderr, "waitfor: warning: failed to get topological path: %s\n",
            zx_status_get_string(status));
    return status;
  }
  const std::string_view got = res->value()->path.get();
  const std::string_view expected(arg);
  if (app.verbose) {
    fprintf(stderr, "waitfor: topological path='%s'\n", std::string(got).c_str());
  }
  // Check if the topo path contains our needle.
  if (got.find(expected) != std::string::npos) {
    return ZX_OK;
  }
  return ZX_ERR_NEXT;
}

zx_status_t expr_part_guid(const App& app, const char* arg, int fd) {
  const fdio_cpp::UnownedFdioCaller caller(fd);
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_partition::Partition>())
          ->GetInstanceGuid();
  if (!result.ok()) {
    fprintf(stderr, "waitfor: warning: cannot request instance guid: %s\n",
            result.FormatDescription().c_str());
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (const zx_status_t status = response.status; status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot get instance guid: %s\n",
            zx_status_get_string(status));
    return result.status();
  }
  fidl::Array value = response.guid->value;
  static_assert(decltype(value)::size() == uuid::kUuidSize);
  const std::string text = uuid::Uuid(value.data()).ToString();
  if (app.verbose) {
    fprintf(stderr, "waitfor: part.guid='%s'\n", text.c_str());
  }
  if (strcasecmp(text.c_str(), arg) == 0) {
    return ZX_OK;
  }
  return ZX_ERR_NEXT;
}

zx_status_t expr_part_type_guid(const App& app, const char* arg, int fd) {
  const fdio_cpp::UnownedFdioCaller caller(fd);
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_partition::Partition>())
          ->GetTypeGuid();
  if (!result.ok()) {
    fprintf(stderr, "waitfor: warning: cannot request type guid: %s\n",
            result.FormatDescription().c_str());
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (const zx_status_t status = response.status; status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot get type guid: %s\n", zx_status_get_string(status));
    return result.status();
  }
  fidl::Array value = response.guid->value;
  static_assert(decltype(value)::size() == uuid::kUuidSize);
  const std::string text = uuid::Uuid(value.data()).ToString();
  if (app.verbose) {
    fprintf(stderr, "waitfor: part.type.guid='%s'\n", text.c_str());
  }
  if (strcasecmp(text.c_str(), arg) == 0) {
    return ZX_OK;
  }
  return ZX_ERR_NEXT;
}

zx_status_t expr_part_name(const App& app, const char* arg, int fd) {
  const fdio_cpp::UnownedFdioCaller caller(fd);
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_partition::Partition>())->GetName();
  if (!result.ok()) {
    fprintf(stderr, "waitfor: warning: cannot request partition name: %s\n",
            result.FormatDescription().c_str());
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (const zx_status_t status = response.status; status != ZX_OK) {
    fprintf(stderr, "waitfor: warning: cannot get type guid: %s\n", zx_status_get_string(status));
    return result.status();
  }
  const std::string name(response.name.get());
  if (app.verbose) {
    fprintf(stderr, "waitfor: part.name='%s'\n", name.c_str());
  }
  if (strcmp(arg, name.c_str()) == 0) {
    return ZX_OK;
  }
  return ZX_ERR_NEXT;
}

int main(int argc, char** argv) {
  if (argc == 1) {
    usage();
    exit(1);
  }

  bool print = false;
  bool verbose = false;
  bool forever = false;
  zx_duration_t timeout = 0;
  const char* devclass = nullptr;
  std::vector<Rule> rules;
  while (argc > 1) {
    if (!strcmp(argv[1], "print")) {
      print = true;
    } else if (!strcmp(argv[1], "verbose")) {
      verbose = true;
    } else if (!strcmp(argv[1], "forever")) {
      forever = true;
    } else if (!strncmp(argv[1], "timeout=", 8)) {
      timeout = ZX_MSEC(atoi(argv[1] + 8));
      if (timeout == 0) {
        fprintf(stderr, "waitfor: error: timeout of 0 not allowed\n");
        exit(1);
      }
    } else if (!strncmp(argv[1], "class=", 6)) {
      devclass = argv[1] + 6;
    } else if (!strncmp(argv[1], "topo=", 5)) {
      rules.emplace_back(argv[1] + 5, expr_topo);
    } else if (!strncmp(argv[1], "part.guid=", 10)) {
      rules.emplace_back(argv[1] + 10, expr_part_guid);
    } else if (!strncmp(argv[1], "part.type.guid=", 15)) {
      rules.emplace_back(argv[1] + 15, expr_part_guid);
    } else if (!strncmp(argv[1], "part.name=", 10)) {
      rules.emplace_back(argv[1] + 10, expr_part_name);
    } else {
      fprintf(stderr, "waitfor: error: unknown expr '%s'\n\n", argv[1]);
      usage();
      exit(1);
    }
    argc--;
    argv++;
  }

  if (devclass == nullptr) {
    fprintf(stderr, "waitfor: error: no class specified\n");
    exit(1);
  }

  if (rules.empty()) {
    fprintf(stderr, "waitfor: error: no match expressions specified\n");
    exit(1);
  }

  const std::string path = std::string("/dev/class/") + devclass;

  const fbl::unique_fd dirfd(open(path.c_str(), O_DIRECTORY | O_RDONLY));
  if (!dirfd.is_valid()) {
    fprintf(stderr, "waitfor: error: cannot watch class '%s': %s\n", devclass, strerror(errno));
    exit(1);
  }

  zx_time_t deadline;
  if (timeout == 0) {
    deadline = ZX_TIME_INFINITE;
  } else {
    deadline = zx_deadline_after(timeout);
  }

  App app = {
      .verbose = verbose,
      .print = print,
      .forever = forever,
      .devclass = devclass,
      .rules = std::move(rules),
  };
  switch (const zx_status_t status = fdio_watch_directory(dirfd.get(), watchcb, deadline, &app);
          status) {
    case ZX_ERR_STOP:
      // clean exit on a match
      return 0;
    case ZX_ERR_TIMED_OUT:
      // timeout, but if we're in forever mode and matched any, its good
      if (app.matched && app.forever) {
        return 0;
      }
      break;
    default:
      // any other situation? failure
      return 1;
  }
}
