// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest_config.h"

#include <libgen.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <iostream>

#include <rapidjson/document.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

// This is a locally administered MAC address (first byte 0x02) mixed with the
// Google Organizationally Unique Identifier (00:1a:11). The host gets ff:ff:ff
// and the guest gets 00:00:00 for the last three octets.
static constexpr fuchsia::hardware::ethernet::MacAddress kGuestMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x00},
};

static void print_usage(fxl::CommandLine& cl) {
  std::cerr << "usage: " << cl.argv0() << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--block=[spec]          Adds a block device with the given parameters\n";
  std::cerr << "\t--cmdline-add=[string]  Adds 'string' to the existing kernel command line.\n";
  std::cerr << "\t                        This will overwrite any existing command line created\n";
  std::cerr << "\t                        using --cmdline or --cmdline-add\n";
  std::cerr << "\t--cmdline=[string]      Use 'string' as the kernel command line\n";
  std::cerr << "\t--cpus=[number]         Number of virtual CPUs available to the guest\n";
  std::cerr << "\t--default-net           Enable a default net device (defaults to true)\n";
  std::cerr << "\t--dtb-overlay=[path]    Load a DTB overlay for a Linux kernel\n";
  std::cerr << "\t--linux=[path]          Load a Linux kernel from 'path'\n";
  std::cerr << "\t--memory=[bytes]        Allocate 'bytes' of memory for the guest.\n";
  std::cerr << "\t                        The suffixes 'k', 'M', and 'G' are accepted\n";
  std::cerr << "\t--net=[spec]            Adds a net device with the given parameters\n";
  std::cerr << "\t--interrupt=[spec]      Adds a hardware interrupt mapping to the guest\n";
  std::cerr << "\t--ramdisk=[path]        Load 'path' as an initial RAM disk\n";
  std::cerr << "\t--virtio-balloon        Enable virtio-balloon (default)\n";
  std::cerr << "\t--virtio-console        Enable virtio-console (default)\n";
  std::cerr << "\t--virtio-gpu            Enable virtio-gpu and virtio-input (default)\n";
  std::cerr << "\t--virtio-rng            Enable virtio-rng (default)\n";
  std::cerr << "\t--virtio-vsock          Enable virtio-vsock (default)\n";
  std::cerr << "\t--zircon=[path]         Load a Zircon kernel from 'path'\n";
  std::cerr << "\n";
  std::cerr << "BLOCK SPEC\n";
  std::cerr << "\n";
  std::cerr << " Block devices can be specified by path:\n";
  std::cerr << "    --block=/pkg/data/disk.img\n";
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
  std::cerr << "      --block=/pkg/data/system.img,fdio,ro\n";
  std::cerr << "\n";
  std::cerr << "  To specify a block device with a given path and read-write\n";
  std::cerr << "  permissions\n";
  std::cerr << "\n";
  std::cerr << "      --block=/dev/class/block/000,fdio,rw\n";
  std::cerr << "\n";
  std::cerr << "NET SPEC\n";
  std::cerr << "\n";
  std::cerr << " Net devices can be specified by MAC address. Each --net argument specifies an\n";
  std::cerr << " additional device.\n";
  std::cerr << "\n";
  std::cerr << " Ex:\n";
  std::cerr << "    --net=02:1a:11:00:00:00\n";
  std::cerr << "\n";
  std::cerr << " By default the guest is configured with one net device with the MAC address in\n";
  std::cerr << " the example above. To remove the default device pass --default-net=false.\n";
  std::cerr << "\n";
}

static GuestConfigParser::OptionHandler set_string(std::string* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    *out = value;
    return ZX_OK;
  };
}

// A function that converts a string option into a custom type.
template <typename T>
using OptionParser = std::function<zx_status_t(const std::string& arg, T* out)>;

// Handles an option by parsing the value and adding it to a container.
template <typename T, typename C>
static GuestConfigParser::OptionHandler add_option(C* out, OptionParser<T> parse) {
  return [out, parse](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T t;
    zx_status_t status = parse(value, &t);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to parse option string '" << value << "'";
      return status;
    }
    out->insert(out->end(), t);
    return ZX_OK;
  };
}

template <typename T>
static GuestConfigParser::OptionHandler set_option(T* out, OptionParser<T> parse) {
  return [out, parse](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T t;
    zx_status_t status = parse(value, &t);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to parse option string '" << value << "'";
      return status;
    }
    *out = t;
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler add_string(std::string* out, const char* delim) {
  return [out, delim](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    out->append(delim);
    out->append(value);
    return ZX_OK;
  };
}

template <typename NumberType>
static zx_status_t parse_number(const std::string& value, NumberType* out, fxl::Base base) {
  if (!fxl::StringToNumberWithError(value, out, base)) {
    FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

template <typename NumberType>
static GuestConfigParser::OptionHandler set_number(NumberType* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    return parse_number(value, out, fxl::Base::k10);
  };
}

// Create an |OptionHandler| that sets |out| to a boolean flag. This can be
// specified not only as '--foo=true' or '--foo=false', but also as '--foo', in
// which case |out| will take the value of |default_flag_value|.
static GuestConfigParser::OptionHandler set_flag(bool* out, bool default_flag_value) {
  return [out, default_flag_value](const std::string& key, const std::string& option_value) {
    bool flag_value = default_flag_value;
    if (!option_value.empty()) {
      if (option_value == "true") {
        flag_value = default_flag_value;
      } else if (option_value == "false") {
        flag_value = !default_flag_value;
      } else {
        FXL_LOG(ERROR) << "Option: '" << key << "' expects either 'true' or 'false'; received '"
                       << option_value << "'";

        return ZX_ERR_INVALID_ARGS;
      }
    }
    *out = flag_value;
    return ZX_OK;
  };
}

static zx_status_t parse_block_spec(const std::string& spec, BlockSpec* out) {
  std::istringstream token_stream(spec);
  std::string token;
  while (std::getline(token_stream, token, ',')) {
    if (token == "fdio") {
      out->format = fuchsia::virtualization::BlockFormat::RAW;
    } else if (token == "qcow") {
      out->format = fuchsia::virtualization::BlockFormat::QCOW;
    } else if (token == "rw") {
      out->mode = fuchsia::virtualization::BlockMode::READ_WRITE;
    } else if (token == "ro") {
      out->mode = fuchsia::virtualization::BlockMode::READ_ONLY;
    } else if (token == "volatile") {
      out->mode = fuchsia::virtualization::BlockMode::VOLATILE_WRITE;
    } else if (token.size() > 0 && token[0] == '/') {
      out->path = std::move(token);
    }
  }
  if (out->path.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

static zx_status_t parse_net_spec(const std::string& input, NetSpec* out) {
  uint32_t bytes[6];
  int r = std::sscanf(input.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0], &bytes[1],
                      &bytes[2], &bytes[3], &bytes[4], &bytes[5]);
  if (r != 6) {
    FXL_LOG(ERROR) << "Couldn't parse MAC address";
    return ZX_ERR_INVALID_ARGS;
  }
  for (size_t i = 0; i != 6; ++i) {
    out->mac_address.octets[i] = static_cast<uint8_t>(bytes[i]);
  }
  return ZX_OK;
}

static std::vector<std::string> split(const std::string& spec, char delim) {
  std::istringstream token_stream(spec);
  std::string token;
  std::vector<std::string> tokens;
  while (std::getline(token_stream, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

static zx_status_t parse_interrupt_spec(const std::string& spec, InterruptSpec* out) {
  std::vector<std::string> tokens = split(spec, ',');
  if (tokens.size() != 2) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = parse_number(tokens[0], &out->vector, fxl::Base::k10);
  if (status != ZX_OK) {
    return status;
  }
  return parse_number(tokens[1], &out->options, fxl::Base::k10);
}

static zx_status_t parse_memory(const std::string& value, size_t* out) {
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

  *out = size;
  return ZX_OK;
}

static zx_status_t parse_memory_spec(const std::string& spec, MemorySpec* out) {
  out->base = 0;
  out->policy = MemoryPolicy::GUEST_CACHED;
  std::vector<std::string> tokens = split(spec, ',');
  if (tokens.size() == 1) {
    return parse_memory(tokens[0], &out->size);
  }
  if (tokens.size() > 3) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = parse_number(tokens[0], &out->base, fxl::Base::k16);
  if (status != ZX_OK) {
    return status;
  }
  status = parse_memory(tokens[1], &out->size);
  if (status != ZX_OK) {
    return status;
  }
  if (tokens.size() != 3) {
    return ZX_OK;
  } else if (tokens[2] == "cached") {
    out->policy = MemoryPolicy::HOST_CACHED;
  } else if (tokens[2] == "device") {
    out->policy = MemoryPolicy::HOST_DEVICE;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

static GuestConfigParser::OptionHandler set_kernel(std::string* out, Kernel* kernel,
                                                   Kernel set_kernel) {
  return [out, kernel, set_kernel](const std::string& key, const std::string& value) {
    zx_status_t status = set_string(out)(key, value);
    if (status == ZX_OK) {
      *kernel = set_kernel;
    }
    return status;
  };
}

GuestConfigParser::GuestConfigParser(GuestConfig* cfg)
    : cfg_(cfg),
      opts_{
          {"block", add_option<BlockSpec>(&cfg_->block_devices_, parse_block_spec)},
          {"cmdline-add", add_string(&cfg_->cmdline_, " ")},
          {"cmdline", set_string(&cfg_->cmdline_)},
          {"cpus", set_number(&cfg_->cpus_)},
          {"dtb-overlay", set_string(&cfg_->dtb_overlay_path_)},
          {"default-net", set_flag(&cfg_->default_net_, true)},
          {"interrupt", add_option<InterruptSpec>(&cfg_->interrupts_, parse_interrupt_spec)},
          {"linux", set_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::LINUX)},
          {"memory", add_option<MemorySpec>(&cfg_->memory_, parse_memory_spec)},
          {"net", add_option<NetSpec>(&cfg_->net_devices_, parse_net_spec)},
          {"ramdisk", set_string(&cfg_->ramdisk_path_)},
          {"virtio-balloon", set_flag(&cfg_->virtio_balloon_, true)},
          {"virtio-console", set_flag(&cfg_->virtio_console_, true)},
          {"virtio-gpu", set_flag(&cfg_->virtio_gpu_, true)},
          {"virtio-magma", set_flag(&cfg_->virtio_magma_, true)},
          {"virtio-rng", set_flag(&cfg_->virtio_rng_, true)},
          {"virtio-vsock", set_flag(&cfg_->virtio_vsock_, true)},
          {"zircon", set_kernel(&cfg_->kernel_path_, &cfg_->kernel_, Kernel::ZIRCON)},
      } {}

void GuestConfigParser::SetDefaults() {
  if (cfg_->memory_.empty()) {
    cfg_->memory_.push_back({.size = 1ul << 30});
  }
  if (cfg_->default_net_) {
    NetSpec default_spec;
    default_spec.mac_address = kGuestMacAddress;
    cfg_->net_devices_.push_back(default_spec);
  }
}

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
      FXL_LOG(ERROR) << "Unknown field in configuration object: " << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }

    // For string members, invoke the handler directly on the value.
    if (member.value.IsString()) {
      zx_status_t status = entry->second(member.name.GetString(), member.value.GetString());
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
        zx_status_t status = entry->second(member.name.GetString(), array_member.GetString());
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
