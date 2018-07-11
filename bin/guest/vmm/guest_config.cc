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
#include "third_party/rapidjson/rapidjson/document.h"

// 32 hex characters + 4 hyphens.
constexpr size_t kGuidStringLen = 36;

static void print_usage(fxl::CommandLine& cl) {
  // clang-format off
  std::cerr << "usage: " << cl.argv0() << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--zircon=[kernel.bin]           Load a Zircon kernel from 'kernel.bin'\n";
  std::cerr << "\t--linux=[kernel.bin]            Load a Linux kernel from 'kernel.bin'\n";
  std::cerr << "\t--ramdisk=[ramdisk.bin]         Use file 'ramdisk.bin' as an initial RAM disk\n";
  std::cerr << "\t--cmdline=[cmdline]             Use string 'cmdline' as the kernel command line. This will\n";
  std::cerr << "\t                                overwrite any existing command line created using --cmdline\n";
  std::cerr << "\t                                or --cmdline-append.\n";
  std::cerr << "\t--cmdline-append=[cmdline]      Appends string 'cmdline' to the existing kernel command\n";
  std::cerr << "\t                                line\n";
  std::cerr << "\t--dtb_overlay=[overlay.dtb]     Load a DTB overlay for a Linux kernel\n";
  std::cerr << "\t--block=[block_spec]            Adds a block device with the given parameters\n";
  std::cerr << "\t--block-wait                    Wait for block devices (specified by GUID) to become\n";
  std::cerr << "\t                                available instead of failing.\n";
  std::cerr << "\t--cpus=[cpus]                   Number of virtual CPUs the guest is allowed to use\n";
  std::cerr << "\t--memory=[bytes]                Allocate 'bytes' of physical memory for the guest. The\n";
  std::cerr << "\t                                suffixes 'k', 'M', and 'G' are accepted\n";
  std::cerr << "\t--balloon-interval=[seconds]    Poll the virtio-balloon device every 'seconds' seconds\n";
  std::cerr << "\t                                and adjust the balloon size based on the amount of\n";
  std::cerr << "\t                                unused guest memory\n";
  std::cerr << "\t--balloon-threshold=[pages]     Number of unused pages to allow the guest to\n";
  std::cerr << "\t                                retain. Has no effect unless -m is also used\n";
  std::cerr << "\t--balloon-demand-page           Demand-page balloon deflate requests\n";
  std::cerr << "\t--display={scenic,framebuffer,  Configures the display backend to use for the guest. 'scenic'\n";
  std::cerr << "\t           none}                (default) will render to a scenic view. 'framebuffer will draw\n";
  std::cerr << "\t                                to a zircon framebuffer. 'none' disables graphical output.\n";
#if __aarch64__
  std::cerr << "\t--gic={2,3}                     Version 2 or 3\n";
#endif
  std::cerr << "\n";
  std::cerr << "BLOCK SPEC\n";
  std::cerr << "\n";
  std::cerr << " Block devices can be specified by path:\n";
  std::cerr << "    /pkg/data/disk.img\n";
  std::cerr << " Or by GPT Partition GUID:\n";
  std::cerr << "    guid:14db42cf-beb7-46a2-9ef8-89b13bb80528,rw\n";
  std::cerr << " Or by GPT Partition Type GUID:\n";
  std::cerr << "    type-guid:4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709,rw\n";
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

// Handles and option by transforming the value and appending it to the
// given vector.
template <typename T>
static GuestConfigParser::OptionHandler append_option(
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
static GuestConfigParser::OptionHandler parse_number(NumberType* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    if (!fxl::StringToNumberWithError(value, out)) {
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

static zx_status_t parse_guid(const std::string& guid_str,
                              machina::BlockDispatcher::Guid& guid) {
  if (!guid.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (guid_str.size() != kGuidStringLen) {
    return ZX_ERR_INVALID_ARGS;
  }

  int ret = sscanf(
      guid_str.c_str(),
      "%2hhx%2hhx%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx%2hhx%"
      "2hhx%2hhx%2hhx",
      &guid.bytes[3], &guid.bytes[2], &guid.bytes[1], &guid.bytes[0],
      &guid.bytes[5], &guid.bytes[4], &guid.bytes[7], &guid.bytes[6],
      &guid.bytes[8], &guid.bytes[9], &guid.bytes[10], &guid.bytes[11],
      &guid.bytes[12], &guid.bytes[13], &guid.bytes[14], &guid.bytes[15]);
  return (ret == 16) ? ZX_OK : ZX_ERR_INVALID_ARGS;
}

static zx_status_t parse_block_spec(const std::string& spec, BlockSpec* out) {
  std::string token;
  std::istringstream tokenStream(spec);
  while (std::getline(tokenStream, token, ',')) {
    if (token == "fdio") {
      out->data_plane = machina::BlockDispatcher::DataPlane::FDIO;
    } else if (token == "qcow") {
      out->data_plane = machina::BlockDispatcher::DataPlane::QCOW;
    } else if (token == "rw") {
      out->mode = machina::BlockDispatcher::Mode::RW;
    } else if (token == "ro") {
      out->mode = machina::BlockDispatcher::Mode::RO;
    } else if (token.size() > 0 && token[0] == '/') {
      out->path = std::move(token);
    } else if (token.compare(0, 5, "guid:") == 0) {
      std::string guid_str = token.substr(5);
      zx_status_t status = parse_guid(guid_str, out->guid);
      if (status != ZX_OK) {
        return status;
      }
      out->guid.type = machina::BlockDispatcher::GuidType::GPT_PARTITION_GUID;
    } else if (token.compare(0, 10, "type-guid:") == 0) {
      std::string guid_str = token.substr(10);
      zx_status_t status = parse_guid(guid_str, out->guid);
      if (status != ZX_OK) {
        return status;
      }
      out->guid.type =
          machina::BlockDispatcher::GuidType::GPT_PARTITION_TYPE_GUID;
    } else if (token == "volatile") {
      out->volatile_writes = true;
    }
  }

  // Path and GUID are mutually exclusive, but one must be provided.
  if (out->path.empty() == out->guid.empty()) {
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

static GuestConfigParser::OptionHandler parse_display(GuestDisplay* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    if (value == "scenic") {
      *out = GuestDisplay::SCENIC;
    } else if (value == "framebuffer") {
      *out = GuestDisplay::FRAMEBUFFER;
    } else if (value == "none") {
      *out = GuestDisplay::NONE;
    } else {
      FXL_LOG(ERROR) << "Invalid display value: " << value;
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  };
}

#if __aarch64__
static GuestConfigParser::OptionHandler parse_gic(machina::GicVersion* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t number;
    if (!fxl::StringToNumberWithError<uint32_t>(value, &number)) {
      FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
      return ZX_ERR_INVALID_ARGS;
    }
    if (number == 2) {
      *out = machina::GicVersion::V2;
    } else if (number == 3) {
      *out = machina::GicVersion::V3;
    } else {
      FXL_LOG(ERROR) << "Invalid GIC version";
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  };
}
#endif

GuestConfigParser::GuestConfigParser(GuestConfig* cfg) : cfg_(cfg), opts_ {
  {"zircon", save_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::ZIRCON)},
      {"linux",
       save_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::LINUX)},
      {"ramdisk", save_option(&cfg_->ramdisk_path_)},
      {"cmdline", save_option(&cfg_->cmdline_)},
      {"cmdline-append", append_string(&cfg_->cmdline_, " ")},
      {"dtb_overlay", save_option(&cfg_->dtb_overlay_path_)},
      {"block",
       append_option<BlockSpec>(&cfg_->block_specs_, parse_block_spec)},
      {"cpus", parse_number(&cfg_->num_cpus_)},
      {"memory", parse_mem_size(&cfg_->memory_)},
      {"balloon-demand-page", set_flag(&cfg_->balloon_demand_page_, true)},
      {"balloon-interval", parse_number(&cfg_->balloon_interval_seconds_)},
      {"balloon-threshold", parse_number(&cfg_->balloon_pages_threshold_)},
      {"display", parse_display(&cfg_->display_)},
      {"network", set_flag(&cfg_->network_, true)},
      {"block-wait", set_flag(&cfg_->block_wait_, true)},
#if __aarch64__
      {"gic", parse_gic(&cfg_->gic_version_)},
#endif
}
{}

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
