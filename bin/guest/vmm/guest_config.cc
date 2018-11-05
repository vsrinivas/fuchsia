// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/guest_config.h"

#include <libgen.h>
#include <unistd.h>
#include <iostream>

#include <zircon/device/block.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "rapidjson/document.h"

static void print_usage(fxl::CommandLine& cl) {
  // clang-format off
  std::cerr << "usage: " << cl.argv0() << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--block=[block_spec]    Adds a block device with the given parameters\n";
  std::cerr << "\t--cmdline-add=[string]  Adds 'string' to the existing kernel command line.\n";
  std::cerr << "\t                        This will overwrite any existing command line created\n";
  std::cerr << "\t                        using --cmdline or --cmdline-add\n";
  std::cerr << "\t--cmdline=[string]      Use 'string' as the kernel command line\n";
  std::cerr << "\t--cpus=[number]         Number of virtual CPUs available to the guest\n";
  std::cerr << "\t--dtb-overlay=[path]    Load a DTB overlay for a Linux kernel\n";
  std::cerr << "\t--host-memory           Directly map host memory into the guest\n";
  std::cerr << "\t--linux=[path]          Load a Linux kernel from 'path'\n";
  std::cerr << "\t--memory=[bytes]        Allocate 'bytes' of memory for the guest.\n";
  std::cerr << "\t                        The suffixes 'k', 'M', and 'G' are accepted\n";
  std::cerr << "\t--ramdisk=[path]        Load 'path' as an initial RAM disk\n";
  std::cerr << "\t--virtio-balloon        Enable virtio-balloon (default)\n";
  std::cerr << "\t--virtio-console        Enable virtio-console (default)\n";
  std::cerr << "\t--virtio-gpu            Enable virtio-gpu and virtio-input (default)\n";
  std::cerr << "\t--virtio-net            Enable virtio-net (default)\n";
  std::cerr << "\t--virtio-rng            Enable virtio-rng (default)\n";
  std::cerr << "\t--virtio-vsock          Enable virtio-vsock (default)\n";
  std::cerr << "\t--virtio-wl             Enable virtio-wl (default)\n";
  std::cerr << "\t--wl-memory=[bytes]     Reserve 'bytes' of memory for Wayland buffers.\n";
  std::cerr << "\t                        The suffixes 'k', 'M', and 'G' are accepted\n";
  std::cerr << "\t--zircon=[path]         Load a Zircon kernel from 'path'\n";
  std::cerr << "\n";
  std::cerr << "BLOCK SPEC\n";
  std::cerr << "\n";
  std::cerr << " Block devices can be specified by path:\n";
  std::cerr << "    /pkg/data/disk.img\n";
  std::cerr << "\n";
  std::cerr << " Additional Options:\n";
  std::cerr << "    rw/ro: Create a read/write or read-only device.\n";
  std::cerr << "    fdio:  Use the FDIO back-end for the block device.\n";
  std::cerr << "\n";
  std::cerr << " Ex:\n";
  std::cerr << "\n";
  std::cerr << "  To open a filesystem resource packaged with the guest application\n";
  std::cerr << "  (read-only is important here as the /pkg/data namespace provides\n";
  std::cerr << "  read-only view into the package resources):\n";
  std::cerr << "\n";
  std::cerr << "      /pkg/data/system.img,fdio,ro\n";
  std::cerr << "\n";
  std::cerr << "  To specify a block device with a given path and read-write\n";
  std::cerr << "  permissions\n";
  std::cerr << "\n";
  std::cerr << "      /dev/class/block/000,fdio,rw\n";
  std::cerr << "\n";
  // clang-format on
}

static GuestConfigParser::OptionHandler save_option(std::string* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    *out = value;
    return ZX_OK;
  };
}

// A function that converts a string option into a custom type.
template <typename T>
using OptionTransform =
    std::function<zx_status_t(const std::string& arg, T* out)>;

// Handles an option by transforming the value and adding it to the vector.
template <typename T>
static GuestConfigParser::OptionHandler add_option(
    std::vector<T>* out, OptionTransform<T> transform) {
  return [out, transform](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T t;
    zx_status_t status = transform(value, &t);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to parse option string '" << value << "'";
      return status;
    }
    out->push_back(t);
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler append_string(std::string* out,
                                                      const char* delim) {
  return [out, delim](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    out->append(delim);
    out->append(value);
    return ZX_OK;
  };
}

constexpr size_t kMinMemorySize = 1 << 20;

static GuestConfigParser::OptionHandler parse_mem_size(size_t* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    char modifier = 'b';
    size_t size;
    int ret = sscanf(value.c_str(), "%zd%c", &size, &modifier);
    if (ret < 1) {
      FXL_LOG(ERROR) << "Value is not a size string: " << value;
      return ZX_ERR_INVALID_ARGS;
    }
    switch (modifier) {
      case 'b':
        break;
      case 'k':
        size *= (1 << 10);
        break;
      case 'M':
        size *= (1 << 20);
        break;
      case 'G':
        size *= (1 << 30);
        break;
      default:
        FXL_LOG(ERROR) << "Invalid size modifier " << modifier;
        return ZX_ERR_INVALID_ARGS;
    }

    if (size < kMinMemorySize) {
      FXL_LOG(ERROR) << "Requested memory " << size
                     << " is less than the minimum supported size "
                     << kMinMemorySize;
      return ZX_ERR_INVALID_ARGS;
    }
    *out = size;
    return ZX_OK;
  };
}

template <typename NumberType>
static GuestConfigParser::OptionHandler parse_number(NumberType* out,
                                                     fxl::Base base) {
  return [out, base](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    if (!fxl::StringToNumberWithError(value, out, base)) {
      FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  };
}

// Create an |OptionHandler| that sets |out| to a boolean flag. This can be
// specified not only as '--foo=true' or '--foo=false', but also as '--foo', in
// which case |out| will take the value of |default_flag_value|.
static GuestConfigParser::OptionHandler set_flag(bool* out,
                                                 bool default_flag_value) {
  return [out, default_flag_value](const std::string& key,
                                   const std::string& option_value) {
    bool flag_value = default_flag_value;
    if (!option_value.empty()) {
      if (option_value == "true") {
        flag_value = default_flag_value;
      } else if (option_value == "false") {
        flag_value = !default_flag_value;
      } else {
        FXL_LOG(ERROR) << "Option: '" << key
                       << "' expects either 'true' or 'false'; received '"
                       << option_value << "'";

        return ZX_ERR_INVALID_ARGS;
      }
    }
    *out = flag_value;
    return ZX_OK;
  };
}

static zx_status_t parse_block_spec(const std::string& spec, BlockSpec* out) {
  std::string token;
  std::istringstream tokenStream(spec);
  while (std::getline(tokenStream, token, ',')) {
    if (token == "fdio") {
      out->format = fuchsia::guest::BlockFormat::RAW;
    } else if (token == "qcow") {
      out->format = fuchsia::guest::BlockFormat::QCOW;
    } else if (token == "rw") {
      out->mode = fuchsia::guest::BlockMode::READ_WRITE;
    } else if (token == "ro") {
      out->mode = fuchsia::guest::BlockMode::READ_ONLY;
    } else if (token == "volatile") {
      out->mode = fuchsia::guest::BlockMode::VOLATILE_WRITE;
    } else if (token.size() > 0 && token[0] == '/') {
      out->path = std::move(token);
    }
  }
  if (out->path.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

static GuestConfigParser::OptionHandler save_kernel(std::string* out,
                                                    Kernel* kernel,
                                                    Kernel set_kernel) {
  return [out, kernel, set_kernel](const std::string& key,
                                   const std::string& value) {
    zx_status_t status = save_option(out)(key, value);
    if (status == ZX_OK) {
      *kernel = set_kernel;
    }
    return status;
  };
}

GuestConfigParser::GuestConfigParser(GuestConfig* cfg)
    : cfg_(cfg),
      opts_{
          {"block",
           add_option<BlockSpec>(&cfg_->block_specs_, parse_block_spec)},
          {"cmdline-add", append_string(&cfg_->cmdline_, " ")},
          {"cmdline", save_option(&cfg_->cmdline_)},
          {"cpus", parse_number(&cfg_->cpus_, fxl::Base::k10)},
          {"dtb-overlay", save_option(&cfg_->dtb_overlay_path_)},
          {"host-memory", set_flag(&cfg_->host_memory_, true)},
          {"linux",
           save_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::LINUX)},
          {"memory", parse_mem_size(&cfg_->memory_)},
          {"ramdisk", save_option(&cfg_->ramdisk_path_)},
          {"virtio-balloon", set_flag(&cfg_->virtio_balloon_, true)},
          {"virtio-console", set_flag(&cfg_->virtio_console_, true)},
          {"virtio-gpu", set_flag(&cfg_->virtio_gpu_, true)},
          {"virtio-net", set_flag(&cfg_->virtio_net_, true)},
          {"virtio-rng", set_flag(&cfg_->virtio_rng_, true)},
          {"virtio-vsock", set_flag(&cfg_->virtio_vsock_, true)},
          {"virtio-wl", set_flag(&cfg_->virtio_wl_, true)},
          {"wl-memory", parse_mem_size(&cfg_->wl_memory_)},
          {"zircon",
           save_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::ZIRCON)},
      } {}

GuestConfigParser::~GuestConfigParser() = default;

zx_status_t GuestConfigParser::ParseArgcArgv(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.positional_args().size() > 0) {
    FXL_LOG(ERROR) << "Unknown positional option: " << cl.positional_args()[0];
    print_usage(cl);
    return ZX_ERR_INVALID_ARGS;
  }

  for (const fxl::CommandLine::Option& option : cl.options()) {
    auto entry = opts_.find(option.name);
    if (entry == opts_.end()) {
      FXL_LOG(ERROR) << "Unknown option --" << option.name;
      print_usage(cl);
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = entry->second(option.name, option.value);
    if (status != ZX_OK) {
      print_usage(cl);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

zx_status_t GuestConfigParser::ParseConfig(const std::string& data) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject()) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (auto& member : document.GetObject()) {
    auto entry = opts_.find(member.name.GetString());
    if (entry == opts_.end()) {
      FXL_LOG(ERROR) << "Unknown field in configuration object: "
                     << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }

    // For string members, invoke the handler directly on the value.
    if (member.value.IsString()) {
      zx_status_t status =
          entry->second(member.name.GetString(), member.value.GetString());
      if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
      continue;
    }

    // For array members, invoke the handler on each value in the array.
    if (member.value.IsArray()) {
      for (auto& array_member : member.value.GetArray()) {
        if (!array_member.IsString()) {
          FXL_LOG(ERROR) << "Array entry has incorect type, expected string: "
                         << member.name.GetString();
          return ZX_ERR_INVALID_ARGS;
        }
        zx_status_t status =
            entry->second(member.name.GetString(), array_member.GetString());
        if (status != ZX_OK) {
          return ZX_ERR_INVALID_ARGS;
        }
      }
      continue;
    }
    FXL_LOG(ERROR) << "Field has incorrect type, expected string or array: "
                   << member.name.GetString();
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}
