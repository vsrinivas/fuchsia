// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest_config.h"

#include <libgen.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <functional>
#include <iostream>
#include <unordered_map>
#include <utility>

#include <rapidjson/document.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/virtualization/bin/vmm/guest.h"

namespace guest_config {

namespace {

using fuchsia::virtualization::GuestConfig;

// This is a locally administered MAC address (first byte 0x02) mixed with the
// Google Organizationally Unique Identifier (00:1a:11). The host gets ff:ff:ff
// and the guest gets 00:00:00 for the last three octets.
static constexpr fuchsia::hardware::ethernet::MacAddress kGuestMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x00},
};

zx_status_t parse(const std::string& name, const std::string& value, bool* result) {
  if (value.empty() || value == "true") {
    *result = true;
  } else if (value == "false") {
    *result = false;
  } else {
    FXL_LOG(ERROR) << "Option '" << name << "' expects either 'true' or 'false'; received '"
                   << value << "'";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t parse(const std::string& name, const std::string& value, std::string* result) {
  *result = value;
  return ZX_OK;
}

template <typename NumberType, fxl::Base base = fxl::Base::k10,
          typename = std::enable_if<std::is_integral<NumberType>::value>>
zx_status_t parse(const std::string& name, const std::string& value, NumberType* out) {
  if (!fxl::StringToNumberWithError(value, out, base)) {
    FXL_LOG(ERROR) << "Option '" << name << "': Unable to convert '" << value << "' into a number";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t parse(const std::string& name, const std::string& value,
                  fuchsia::virtualization::BlockSpec* out) {
  std::istringstream token_stream(value);
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

std::vector<std::string> split(const std::string& spec, char delim) {
  std::istringstream token_stream(spec);
  std::string token;
  std::vector<std::string> tokens;
  while (std::getline(token_stream, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

zx_status_t parse_memory(const std::string& value, size_t* out) {
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

zx_status_t parse(const std::string& name, const std::string& value,
                  fuchsia::virtualization::MemorySpec* out) {
  out->base = 0;
  out->policy = fuchsia::virtualization::MemoryPolicy::GUEST_CACHED;
  std::vector<std::string> tokens = split(value, ',');
  if (tokens.size() == 1) {
    return parse_memory(tokens[0], &out->size);
  }
  if (tokens.size() > 3) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = parse<uint64_t, fxl::Base::k16>(name, tokens[0], &out->base);
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
    out->policy = fuchsia::virtualization::MemoryPolicy::HOST_CACHED;
  } else if (tokens[2] == "device") {
    out->policy = fuchsia::virtualization::MemoryPolicy::HOST_DEVICE;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t parse(const std::string& name, const std::string& value,
                  fuchsia::virtualization::NetSpec* out) {
  uint32_t bytes[6];
  int r = std::sscanf(value.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0], &bytes[1],
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

class OptionHandler {
 public:
  OptionHandler() : field_exists_{[](const GuestConfig& _) { return false; }} {}
  OptionHandler(std::function<bool(const GuestConfig&)> field_exists)
      : field_exists_{field_exists} {}

  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value,
                  bool override) {
    if (override || !field_exists_(*cfg)) {
      return SetInternal(cfg, name, value);
    }
    return ZX_OK;
  }

  void MaybeSetDefault(GuestConfig* cfg) {
    if (!field_exists_(*cfg)) {
      SetDefault(cfg);
    }
  }

  virtual ~OptionHandler() = default;

 protected:
  virtual zx_status_t SetInternal(GuestConfig* cfg, const std::string& name,
                                  const std::string& value) = 0;
  virtual void SetDefault(GuestConfig* cfg) {}

  std::function<bool(const GuestConfig&)> field_exists_;
};

template <typename T, bool require_value = true>
class OptionHandlerWithoutDefaultValue : public OptionHandler {
 public:
  OptionHandlerWithoutDefaultValue(std::function<bool(const GuestConfig&)> field_exists,
                                   std::function<T*(GuestConfig*)> mutable_field)
      : OptionHandler{field_exists}, mutable_field_{std::move(mutable_field)} {}

 protected:
  void FillMutableField(const T& value, GuestConfig* cfg) { *(mutable_field_(cfg)) = value; }

  zx_status_t SetInternal(GuestConfig* cfg, const std::string& name,
                          const std::string& value) override {
    if (require_value && value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << name << "' expects a value (--" << name << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T result;
    auto status = parse(name, value, &result);
    if (status == ZX_OK) {
      FillMutableField(result, cfg);
    }
    return status;
  }

 private:
  std::function<T*(GuestConfig*)> mutable_field_;
};

template <typename T, bool require_value = true>
class OptionHandlerWithDefaultValue : public OptionHandlerWithoutDefaultValue<T, require_value> {
 public:
  OptionHandlerWithDefaultValue(std::function<bool(const GuestConfig&)> field_exists,
                                std::function<T*(GuestConfig*)> mutable_field,
                                const T& default_value)
      : OptionHandlerWithoutDefaultValue<T, require_value>{std::move(field_exists),
                                                           std::move(mutable_field)},
        default_value_{default_value} {}

 protected:
  void SetDefault(GuestConfig* cfg) override {
    OptionHandlerWithoutDefaultValue<T, require_value>::FillMutableField(default_value_, cfg);
  }

  const T default_value_;
};

class NumCpusOptionHandler : public OptionHandlerWithDefaultValue<uint8_t> {
 public:
  using OptionHandlerWithDefaultValue<uint8_t>::OptionHandlerWithDefaultValue;

 protected:
  zx_status_t SetInternal(GuestConfig* cfg, const std::string& name,
                          const std::string& value) override {
    zx_status_t status = OptionHandlerWithoutDefaultValue::SetInternal(cfg, name, value);
    if (status == ZX_OK && cfg->cpus() > Guest::kMaxVcpus) {
      FXL_LOG(ERROR) << "Option: '" << name << "' expects a value <= " << Guest::kMaxVcpus;
      return ZX_ERR_INVALID_ARGS;
    }
    return status;
  }
};

using BoolOptionHandler = OptionHandlerWithDefaultValue<bool, /* require_value= */ false>;
using StringOptionHandler = OptionHandlerWithoutDefaultValue<std::string>;

class KernelOptionHandler : public StringOptionHandler {
 public:
  KernelOptionHandler(
      std::function<bool(const GuestConfig&)> field_exists,
      std::function<std::string*(GuestConfig*)> mutable_field,
      std::function<fuchsia::virtualization::Kernel*(GuestConfig*)> mutable_kernel_fn,
      fuchsia::virtualization::Kernel kernel_kind)
      : StringOptionHandler{std::move(field_exists), std::move(mutable_field)},
        mutable_kernel_fn_{std::move(mutable_kernel_fn)},
        kernel_kind_{kernel_kind} {}

 private:
  zx_status_t SetInternal(GuestConfig* cfg, const std::string& name,
                          const std::string& value) override {
    auto result = StringOptionHandler::SetInternal(cfg, name, value);
    if (result == ZX_OK) {
      *(mutable_kernel_fn_(cfg)) = kernel_kind_;
    }
    return result;
  }

  std::function<fuchsia::virtualization::Kernel*(GuestConfig*)> mutable_kernel_fn_;
  fuchsia::virtualization::Kernel kernel_kind_;
};

template <typename T>
class RepeatedOptionHandler : public OptionHandler {
 public:
  RepeatedOptionHandler(std::function<std::vector<T>*(GuestConfig*)> mutable_field)
      : mutable_field_{std::move(mutable_field)} {}

 protected:
  zx_status_t SetInternal(GuestConfig* cfg, const std::string& name,
                          const std::string& value) override {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << name << "' expects a value (--" << name << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T result;
    auto status = parse(name, value, &result);
    if (status == ZX_OK) {
      mutable_field_(cfg)->emplace_back(result);
    }
    return status;
  }

  void SetDefault(GuestConfig* cfg) override {
    mutable_field_(cfg);  // Fills it with an empty vector.
  }

 private:
  std::function<std::vector<T>*(GuestConfig*)> mutable_field_;
};

std::unordered_map<std::string, std::unique_ptr<OptionHandler>> GetCmdlineOptionHanders() {
  std::unordered_map<std::string, std::unique_ptr<OptionHandler>> handlers;
  handlers.emplace("cmdline-add", std::make_unique<RepeatedOptionHandler<std::string>>(
                                      &GuestConfig::mutable_cmdline_add));
  handlers.emplace("cmdline", std::make_unique<StringOptionHandler>(&GuestConfig::has_cmdline,
                                                                    &GuestConfig::mutable_cmdline));
  handlers.emplace(
      "cpus", std::make_unique<NumCpusOptionHandler>(
                  &GuestConfig::has_cpus, &GuestConfig::mutable_cpus, zx_system_get_num_cpus()));
  handlers.emplace("interrupt", std::make_unique<RepeatedOptionHandler<uint32_t>>(
                                    &GuestConfig::mutable_interrupts));
  handlers.emplace("memory",
                   std::make_unique<RepeatedOptionHandler<fuchsia::virtualization::MemorySpec>>(
                       &GuestConfig::mutable_memory));
  handlers.emplace("net", std::make_unique<RepeatedOptionHandler<fuchsia::virtualization::NetSpec>>(
                              &GuestConfig::mutable_net_devices));
  handlers.emplace("default-net",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_default_net,
                                                       &GuestConfig::mutable_default_net, true));
  handlers.emplace("virtio-balloon",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_balloon,
                                                       &GuestConfig::mutable_virtio_balloon, true));
  handlers.emplace("virtio-console",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_console,
                                                       &GuestConfig::mutable_virtio_console, true));
  handlers.emplace("virtio-gpu",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_gpu,
                                                       &GuestConfig::mutable_virtio_gpu, true));
  handlers.emplace("virtio-magma",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_magma,
                                                       &GuestConfig::mutable_virtio_magma, true));
  handlers.emplace("virtio-rng",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_rng,
                                                       &GuestConfig::mutable_virtio_rng, true));
  handlers.emplace("virtio-vsock",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::has_virtio_vsock,
                                                       &GuestConfig::mutable_virtio_vsock, true));
  return handlers;
}

std::unordered_map<std::string, std::unique_ptr<OptionHandler>> GetAllOptionHandlers() {
  auto handlers = GetCmdlineOptionHanders();
  handlers.emplace("block",
                   std::make_unique<RepeatedOptionHandler<fuchsia::virtualization::BlockSpec>>(
                       &GuestConfig::mutable_block_devices));
  handlers.emplace("dtb-overlay",
                   std::make_unique<StringOptionHandler>(&GuestConfig::has_dtb_overlay_path,
                                                         &GuestConfig::mutable_dtb_overlay_path));
  handlers.emplace("linux",
                   std::make_unique<KernelOptionHandler>(
                       &GuestConfig::has_kernel_path, &GuestConfig::mutable_kernel_path,
                       &GuestConfig::mutable_kernel, fuchsia::virtualization::Kernel::LINUX));
  handlers.emplace("ramdisk",
                   std::make_unique<StringOptionHandler>(&GuestConfig::has_ramdisk_path,
                                                         &GuestConfig::mutable_ramdisk_path));
  handlers.emplace("zircon",
                   std::make_unique<KernelOptionHandler>(
                       &GuestConfig::has_kernel_path, &GuestConfig::mutable_kernel_path,
                       &GuestConfig::mutable_kernel, fuchsia::virtualization::Kernel::ZIRCON));
  return handlers;
}

}  // namespace

void PrintCommandLineUsage(const char* program_name) {
  // clang-format off
  std::cerr << "usage: " << program_name << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--cmdline-add=[string]  Adds 'string' to the existing kernel command line.\n";
  std::cerr << "\t                        This will overwrite any existing command line created\n";
  std::cerr << "\t                        using --cmdline or --cmdline-add\n";
  std::cerr << "\t--cmdline=[string]      Use 'string' as the kernel command line\n";
  std::cerr << "\t--cpus=[number]         Number of virtual CPUs available to the guest\n";
  std::cerr << "\t--default-net           Enable a default net device (defaults to true)\n";
  std::cerr << "\t--memory=[bytes]        Allocate 'bytes' of memory for the guest.\n";
  std::cerr << "\t                        The suffixes 'k', 'M', and 'G' are accepted\n";
  std::cerr << "\t--net=[spec]            Adds a net device with the given parameters\n";
  std::cerr << "\t--interrupt=[spec]      Adds a hardware interrupt mapping to the guest\n";
  std::cerr << "\t--virtio-balloon        Enable virtio-balloon (default)\n";
  std::cerr << "\t--virtio-console        Enable virtio-console (default)\n";
  std::cerr << "\t--virtio-gpu            Enable virtio-gpu and virtio-input (default)\n";
  std::cerr << "\t--virtio-rng            Enable virtio-rng (default)\n";
  std::cerr << "\t--virtio-vsock          Enable virtio-vsock (default)\n";
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
  // clang-format on
}

void SetDefaults(GuestConfig* cfg) {
  if (!cfg->has_memory()) {
    cfg->mutable_memory()->push_back({.size = 1ul << 30});
  }

  for (const auto& [name, handler] : GetAllOptionHandlers()) {
    handler->MaybeSetDefault(cfg);
  }

  // This is required as a cmdline-add on the command-line arguments needs to work in combination
  // with a cmdline from the parsed json file.
  for (const auto& item : cfg->cmdline_add()) {
    *cfg->mutable_cmdline() += " " + item;
  }
  cfg->clear_cmdline_add();

  if (cfg->default_net()) {
    cfg->mutable_net_devices()->push_back({.mac_address = kGuestMacAddress});
  }
}

zx_status_t ParseArguments(int argc, const char** argv, fuchsia::virtualization::GuestConfig* cfg) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.positional_args().size() > 0) {
    FXL_LOG(ERROR) << "Unknown positional option: " << cl.positional_args()[0];
    return ZX_ERR_INVALID_ARGS;
  }

  auto handlers = GetCmdlineOptionHanders();
  for (const fxl::CommandLine::Option& option : cl.options()) {
    auto entry = handlers.find(option.name);
    if (entry == handlers.end()) {
      FXL_LOG(ERROR) << "Unknown option --" << option.name;
      return ZX_ERR_INVALID_ARGS;
    }
    auto status = entry->second->Set(cfg, option.name, option.value, /* override= */ true);
    if (status != ZX_OK) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

zx_status_t ParseConfig(const std::string& data, GuestConfig* cfg) {
  // To maintain compatibility with existing code, any cmdline added by the config file gets
  // prepended to the cmdline provided by the user.
  if (cfg->has_cmdline()) {
    cfg->mutable_cmdline_add()->insert(cfg->cmdline_add().begin(), cfg->cmdline());
    cfg->clear_cmdline();
  }

  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject()) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto opts = GetAllOptionHandlers();
  for (auto& member : document.GetObject()) {
    auto entry = opts.find(member.name.GetString());
    if (entry == opts.end()) {
      FXL_LOG(ERROR) << "Unknown field in configuration object: " << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }

    // For string members, invoke the handler directly on the value.
    if (member.value.IsString()) {
      zx_status_t status = entry->second->Set(cfg, member.name.GetString(),
                                              member.value.GetString(), /* override= */ false);
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
        zx_status_t status = entry->second->Set(cfg, member.name.GetString(),
                                                array_member.GetString(), /* override= */ true);
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

}  // namespace guest_config
