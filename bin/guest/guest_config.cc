// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_config.h"

#include <libgen.h>
#include <unistd.h>
#include <iostream>

#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "third_party/rapidjson/rapidjson/document.h"

static void print_usage(fxl::CommandLine& cl) {
  // clang-format off
  std::cerr << "usage: " << cl.argv0() << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--kernel=[kernel.bin]           Use file 'kernel.bin' as the kernel\n";
  std::cerr << "\t--ramdisk=[ramdisk.bin]         Use file 'ramdisk.bin' as a ramdisk\n";
  std::cerr << "\t--block=[block.bin]             Use file 'block.bin' as a virtio-block device\n";
  std::cerr << "\t--cmdline=[cmdline]             Use string 'cmdline' as the kernel command line\n";
  std::cerr << "\t--balloon-interval=[seconds]    Poll the virtio-balloon device every 'seconds' seconds\n";
  std::cerr << "\t                                and adjust the balloon size based on the amount of\n";
  std::cerr << "\t                                unused guest memory\n";
  std::cerr << "\t--balloon-threshold=[pages]     Number of unused pages to allow the guest to\n";
  std::cerr << "\t                                retain. Has no effect unless -m is also used\n";
  std::cerr << "\t--balloon-demand-page           Demand-page balloon deflate requests\n";
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
        flag_value = true;
      } else if (option_value == "false") {
        flag_value = false;
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

GuestConfig::GuestConfig()
    : kernel_path_("/pkg/data/kernel"), ramdisk_path_("/pkg/data/ramdisk") {}

GuestConfig::~GuestConfig() = default;

GuestConfigParser::GuestConfigParser(GuestConfig* config)
    : config_(config),
      options_{
          {"kernel", save_option(&config_->kernel_path_)},
          {"ramdisk", save_option(&config_->ramdisk_path_)},
          {"block", save_option(&config_->block_path_)},
          {"cmdline", save_option(&config_->cmdline_)},
          {"balloon-demand-page",
           set_flag(&config_->balloon_demand_page_, true)},
          {"balloon-interval",
           parse_number(&config_->balloon_interval_seconds_)},
          {"balloon-threshold",
           parse_number(&config_->balloon_pages_threshold_)},
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
    auto entry = options_.find(option.name);
    if (entry == options_.end()) {
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
    auto entry = options_.find(member.name.GetString());
    if (entry == options_.end()) {
      FXL_LOG(ERROR) << "Unknown field in configuration object: "
                     << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }
    if (!member.value.IsString()) {
      FXL_LOG(ERROR) << "Field has incorrect type, expected string: "
                     << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status =
        entry->second(member.name.GetString(), member.value.GetString());
    if (status != ZX_OK) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}
