// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_config/guest_config.h"

#include <lib/syslog/cpp/macros.h>
#include <libgen.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <functional>
#include <iostream>
#include <unordered_map>
#include <utility>

#include <rapidjson/document.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace guest_config {

namespace {

using fuchsia::virtualization::GuestConfig;

zx_status_t parse(const std::string& name, const std::string& value, bool* result) {
  if (value.empty() || value == "true") {
    *result = true;
  } else if (value == "false") {
    *result = false;
  } else {
    FX_LOGS(ERROR) << "Option '" << name << "' expects either 'true' or 'false'; received '"
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
    FX_LOGS(ERROR) << "Option '" << name << "': Unable to convert '" << value << "' into a number";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t parse(const OpenAt& open_at, const std::string& name, const std::string& value,
                  fuchsia::virtualization::BlockSpec* out) {
  std::string path;
  std::istringstream token_stream(value);
  std::string token;
  while (std::getline(token_stream, token, ',')) {
    if (token == "rw") {
      out->mode = fuchsia::virtualization::BlockMode::READ_WRITE;
    } else if (token == "ro") {
      out->mode = fuchsia::virtualization::BlockMode::READ_ONLY;
    } else if (token == "volatile") {
      out->mode = fuchsia::virtualization::BlockMode::VOLATILE_WRITE;
    } else if (token == "file") {
      out->format = fuchsia::virtualization::BlockFormat::FILE;
    } else if (token == "qcow") {
      out->format = fuchsia::virtualization::BlockFormat::QCOW;
    } else if (token == "block") {
      out->format = fuchsia::virtualization::BlockFormat::BLOCK;
    } else {
      // Set the last MAX_BLOCK_DEVICE_ID characters of token as the ID.
      size_t pos = token.size() > fuchsia::virtualization::MAX_BLOCK_DEVICE_ID
                       ? token.size() - fuchsia::virtualization::MAX_BLOCK_DEVICE_ID
                       : 0;
      out->id = token.substr(pos, fuchsia::virtualization::MAX_BLOCK_DEVICE_ID);
      path = std::move(token);
    }
  }
  if (path.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx::channel server;
  zx_status_t status = zx::channel::create(0, &out->client, &server);
  if (status != ZX_OK) {
    return status;
  }
  return open_at(path, fidl::InterfaceRequest<fuchsia::io::File>(std::move(server)));
}

zx_status_t parse(const std::string& name, const std::string& value, uint64_t* out) {
  char modifier = 'b';
  uint64_t size;
  int ret = sscanf(value.c_str(), "%lu%c", &size, &modifier);
  if (ret < 1) {
    FX_LOGS(ERROR) << "Value is not a size string: " << value;
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
      FX_LOGS(ERROR) << "Invalid size modifier " << modifier;
      return ZX_ERR_INVALID_ARGS;
  }

  *out = size;
  return ZX_OK;
}

class OptionHandler {
 public:
  OptionHandler() = default;

  virtual zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) = 0;

  virtual ~OptionHandler() = default;
};

template <typename T>
class SimpleOptionHandler : public OptionHandler {
 public:
  SimpleOptionHandler(std::function<T*(GuestConfig*)> mutable_field)
      : OptionHandler(), mutable_field_{std::move(mutable_field)} {}

 protected:
  void FillMutableField(const T& value, GuestConfig* cfg) { *(mutable_field_(cfg)) = value; }

  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) override {
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

using GuestMemoryOptionHandler = SimpleOptionHandler<uint64_t>;
using NumCpusOptionHandler = SimpleOptionHandler<uint8_t>;
using BoolOptionHandler = SimpleOptionHandler<bool>;
using StringOptionHandler = SimpleOptionHandler<std::string>;

class FileOptionHandler : public OptionHandler {
 public:
  FileOptionHandler(OpenAt open_at,
                    std::function<fuchsia::io::FileHandle*(GuestConfig*)> mutable_field)
      : OptionHandler(), open_at_{std::move(open_at)}, mutable_field_{std::move(mutable_field)} {}

 protected:
  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) override {
    if (value.empty()) {
      FX_LOGS(ERROR) << "Option: '" << name << "' expects a value (--" << name << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    fuchsia::io::FileHandle result;
    zx_status_t status = open_at_(value, result.NewRequest());
    if (status == ZX_OK) {
      *(mutable_field_(cfg)) = std::move(result);
    }
    return status;
  }

 private:
  OpenAt open_at_;
  std::function<fuchsia::io::FileHandle*(GuestConfig*)> mutable_field_;
};

class KernelOptionHandler : public FileOptionHandler {
 public:
  KernelOptionHandler(
      OpenAt open_at, std::function<fuchsia::io::FileHandle*(GuestConfig*)> mutable_field,
      std::function<fuchsia::virtualization::KernelType*(GuestConfig*)> mutable_type_fn,
      fuchsia::virtualization::KernelType type)
      : FileOptionHandler{std::move(open_at), std::move(mutable_field)},
        mutable_type_fn_{std::move(mutable_type_fn)},
        type_{type} {}

 private:
  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) override {
    auto status = FileOptionHandler::Set(cfg, name, value);
    if (status == ZX_OK) {
      *(mutable_type_fn_(cfg)) = type_;
    }
    return status;
  }

  std::function<fuchsia::virtualization::KernelType*(GuestConfig*)> mutable_type_fn_;
  fuchsia::virtualization::KernelType type_;
};

template <typename T>
class RepeatedOptionHandler : public OptionHandler {
 public:
  RepeatedOptionHandler(std::function<std::vector<T>*(GuestConfig*)> mutable_field)
      : mutable_field_{std::move(mutable_field)} {}

 protected:
  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) override {
    if (value.empty()) {
      FX_LOGS(ERROR) << "Option: '" << name << "' expects a value (--" << name << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T result{};
    auto status = parse(name, value, &result);
    if (status == ZX_OK) {
      mutable_field_(cfg)->emplace_back(result);
    }
    return status;
  }

 private:
  std::function<std::vector<T>*(GuestConfig*)> mutable_field_;
};

template <>
class RepeatedOptionHandler<fuchsia::virtualization::BlockSpec> : public OptionHandler {
 public:
  RepeatedOptionHandler(
      OpenAt open_at,
      std::function<std::vector<fuchsia::virtualization::BlockSpec>*(GuestConfig*)> mutable_field)
      : open_at_(std::move(open_at)), mutable_field_{std::move(mutable_field)} {}

 protected:
  zx_status_t Set(GuestConfig* cfg, const std::string& name, const std::string& value) override {
    if (value.empty()) {
      FX_LOGS(ERROR) << "Option: '" << name << "' expects a value (--" << name << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    fuchsia::virtualization::BlockSpec result;
    auto status = parse(open_at_, name, value, &result);
    if (status == ZX_OK) {
      mutable_field_(cfg)->emplace_back(std::move(result));
    }
    return status;
  }

 private:
  OpenAt open_at_;
  std::function<std::vector<fuchsia::virtualization::BlockSpec>*(GuestConfig*)> mutable_field_;
};

std::unordered_map<std::string, std::unique_ptr<OptionHandler>> GetAllOptionHandlers(
    OpenAt open_at) {
  std::unordered_map<std::string, std::unique_ptr<OptionHandler>> handlers;
  handlers.emplace("block",
                   std::make_unique<RepeatedOptionHandler<fuchsia::virtualization::BlockSpec>>(
                       open_at.share(), &GuestConfig::mutable_block_devices));
  handlers.emplace("cmdline", std::make_unique<StringOptionHandler>(&GuestConfig::mutable_cmdline));
  handlers.emplace("dtb-overlay", std::make_unique<FileOptionHandler>(
                                      open_at.share(), &GuestConfig::mutable_dtb_overlay));
  handlers.emplace(
      "linux", std::make_unique<KernelOptionHandler>(open_at.share(), &GuestConfig::mutable_kernel,
                                                     &GuestConfig::mutable_kernel_type,
                                                     fuchsia::virtualization::KernelType::LINUX));
  handlers.emplace("ramdisk", std::make_unique<FileOptionHandler>(open_at.share(),
                                                                  &GuestConfig::mutable_ramdisk));
  handlers.emplace(
      "zircon", std::make_unique<KernelOptionHandler>(open_at.share(), &GuestConfig::mutable_kernel,
                                                      &GuestConfig::mutable_kernel_type,
                                                      fuchsia::virtualization::KernelType::ZIRCON));
  handlers.emplace("cmdline-add", std::make_unique<RepeatedOptionHandler<std::string>>(
                                      &GuestConfig::mutable_cmdline_add));
  handlers.emplace("memory",
                   std::make_unique<GuestMemoryOptionHandler>(&GuestConfig::mutable_guest_memory));
  handlers.emplace("cpus", std::make_unique<NumCpusOptionHandler>(&GuestConfig::mutable_cpus));
  handlers.emplace("default-net",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_default_net));
  handlers.emplace("virtio-balloon",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_balloon));
  handlers.emplace("virtio-console",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_console));
  handlers.emplace("virtio-gpu",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_gpu));
  handlers.emplace("virtio-rng",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_rng));
  handlers.emplace("virtio-sound",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_sound));
  handlers.emplace("virtio-sound-input",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_sound_input));
  handlers.emplace("virtio-vsock",
                   std::make_unique<BoolOptionHandler>(&GuestConfig::mutable_virtio_vsock));
  return handlers;
}

}  // namespace

zx::result<GuestConfig> ParseConfig(const std::string& data, OpenAt open_at) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  GuestConfig cfg;
  auto opts = GetAllOptionHandlers(std::move(open_at));
  for (auto& member : document.GetObject()) {
    auto entry = opts.find(member.name.GetString());
    if (entry == opts.end()) {
      FX_LOGS(ERROR) << "Unknown field in configuration object: " << member.name.GetString();
      return zx::error(ZX_ERR_INVALID_ARGS);
    }

    // For string members, invoke the handler directly on the value.
    if (member.value.IsString()) {
      zx_status_t status =
          entry->second->Set(&cfg, member.name.GetString(), member.value.GetString());
      if (status != ZX_OK) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      continue;
    }

    // For array members, invoke the handler on each value in the array.
    if (member.value.IsArray()) {
      for (auto& array_member : member.value.GetArray()) {
        if (!array_member.IsString()) {
          FX_LOGS(ERROR) << "Array entry has incorect type, expected string: "
                         << member.name.GetString();
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
        zx_status_t status =
            entry->second->Set(&cfg, member.name.GetString(), array_member.GetString());
        if (status != ZX_OK) {
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
      }
      continue;
    }
    FX_LOGS(ERROR) << "Field has incorrect type, expected string or array: "
                   << member.name.GetString();
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(std::move(cfg));
}

fuchsia::virtualization::GuestConfig MergeConfigs(fuchsia::virtualization::GuestConfig base,
                                                  fuchsia::virtualization::GuestConfig overrides) {
#define COPY_GUEST_CONFIG_FIELD(field_name)                                \
  do {                                                                     \
    if (overrides.has_##field_name()) {                                    \
      base.set_##field_name(std::move(*overrides.mutable_##field_name())); \
    }                                                                      \
  } while (0)
#define APPEND_GUEST_CONFIG_FIELD(field_name)                                 \
  do {                                                                        \
    if (overrides.has_##field_name()) {                                       \
      base.mutable_##field_name()->insert(                                    \
          base.mutable_##field_name()->end(),                                 \
          std::make_move_iterator(overrides.mutable_##field_name()->begin()), \
          std::make_move_iterator(overrides.mutable_##field_name()->end()));  \
    }                                                                         \
  } while (0)

  COPY_GUEST_CONFIG_FIELD(kernel_type);
  COPY_GUEST_CONFIG_FIELD(kernel);
  COPY_GUEST_CONFIG_FIELD(ramdisk);
  COPY_GUEST_CONFIG_FIELD(dtb_overlay);
  COPY_GUEST_CONFIG_FIELD(cmdline);
  APPEND_GUEST_CONFIG_FIELD(cmdline_add);
  COPY_GUEST_CONFIG_FIELD(cpus);
  COPY_GUEST_CONFIG_FIELD(guest_memory);
  APPEND_GUEST_CONFIG_FIELD(block_devices);
  APPEND_GUEST_CONFIG_FIELD(net_devices);
  COPY_GUEST_CONFIG_FIELD(wayland_device);
  COPY_GUEST_CONFIG_FIELD(magma_device);
  COPY_GUEST_CONFIG_FIELD(default_net);
  COPY_GUEST_CONFIG_FIELD(virtio_balloon);
  COPY_GUEST_CONFIG_FIELD(virtio_console);
  COPY_GUEST_CONFIG_FIELD(virtio_gpu);
  COPY_GUEST_CONFIG_FIELD(virtio_rng);
  COPY_GUEST_CONFIG_FIELD(virtio_vsock);
  COPY_GUEST_CONFIG_FIELD(virtio_sound);
  COPY_GUEST_CONFIG_FIELD(virtio_sound_input);
  APPEND_GUEST_CONFIG_FIELD(vsock_listeners);

#undef COPY_GUEST_CONFIG_FIELD
#undef APPEND_GUEST_CONFIG_FIELD

  return base;
}

}  // namespace guest_config
