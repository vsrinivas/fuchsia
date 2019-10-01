// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/types.h>

#include "intel_hda_codec.h"
#include "intel_hda_controller.h"
#include "zircon_device.h"

namespace audio {
namespace intel_hda {

static IntelHDAController::ControllerTree::iterator GetController(int id) {
  return (id < 0) ? IntelHDAController::controllers().begin()
                  : IntelHDAController::controllers().find(id);
}

static IntelHDACodec::CodecTree::iterator GetCodec(int id) {
  return (id < 0) ? IntelHDACodec::codecs().begin() : IntelHDACodec::codecs().find(id);
}

int main(int argc, const char** argv) {
  int arg = 1;
  int32_t dev_id = 0;
  int32_t codec_id = 0;
  zx_status_t res;

  while (arg < argc) {
    if (!strcmp("-d", argv[arg])) {
      if ((++arg >= argc) || sscanf(argv[arg++], "%d", &dev_id) != 1)
        goto usage;

      if (dev_id < 0)
        goto usage;
    } else if (!strcmp("-c", argv[arg])) {
      if ((++arg >= argc) || sscanf(argv[arg++], "%d", &codec_id) != 1)
        goto usage;

      if (codec_id < 0)
        goto usage;
    } else
      break;
  }

  if (arg >= argc)
    goto usage;

  res = IntelHDAController::Enumerate();
  if (res != ZX_OK) {
    printf("Failed to enumerate controller devices (%d)\n", res);
    return res;
  }

  res = IntelHDACodec::Enumerate();
  if (res != ZX_OK) {
    printf("Failed to enumerate codec devices (%d)\n", res);
    return res;
  }

  if (!strcmp("list", argv[arg])) {
    printf("Found %zu Intel HDA Controllers\n", IntelHDAController::controllers().size());
    for (auto& controller : IntelHDAController::controllers()) {
      IntelHDADevice device;
      res = controller.Probe(&device);
      if (res != ZX_OK) {
        printf("Failed to probe controller at \"%s\" (res %d)\n", controller.dev_name().c_str(),
               res);
        return res;
      }

      controller.Disconnect();

      printf("device %u [%04hx:%04hx %u.%u] : %s\n", controller.id(), device.vid, device.did,
             device.ihda_vmaj, device.ihda_vmin, controller.dev_name().c_str());
    }

    printf("Found %zu Intel HDA Codecs\n", IntelHDACodec::codecs().size());
    for (auto& codec : IntelHDACodec::codecs()) {
      IntelHDADevice device;
      res = codec.Probe(&device);

      if (res != ZX_OK) {
        printf("Failed to probe codec at \"%s\" (res %d)\n", codec.dev_name().c_str(), res);
        return res;
      }

      printf("  Codec %u [%04hx:%04hx] : %s\n", codec.id(), device.vid, device.did,
             codec.dev_name().c_str());

      codec.Disconnect();
    }

    return 0;
  }

  static const struct {
    const char* name;
    zx_status_t (IntelHDAController::*cmd)(int, const char**);
  } CONTROLLER_CMDS[] = {
      {"regs", &IntelHDAController::DumpRegs},
  };

  for (const auto& cmd : CONTROLLER_CMDS) {
    if (strcmp(cmd.name, argv[arg]))
      continue;

    auto iter = GetController(dev_id);

    if (!iter.IsValid()) {
      printf("Intel HDA controller not found!\n");
      return ZX_ERR_NOT_FOUND;
    }

    arg++;
    return ((*iter).*cmd.cmd)(argc - arg, argv + arg);
  }

  static const struct {
    const char* name;
    zx_status_t (IntelHDACodec::*cmd)(int, const char**);
  } CODEC_CMDS[] = {
      {"codec", &IntelHDACodec::DumpCodec},
  };

  for (const auto& cmd : CODEC_CMDS) {
    if (strcmp(cmd.name, argv[arg]))
      continue;

    auto iter = GetCodec(codec_id);

    if (!iter.IsValid()) {
      printf("Intel HDA codec not found!\n");
      return ZX_ERR_NOT_FOUND;
    }

    arg++;
    return ((*iter).*cmd.cmd)(argc - arg, argv + arg);
  }

usage:
  printf(
      "usage: %s [-d <dev_id>] [-c <codec_id>] <cmd>\n"
      "Valid cmds are...\n"
      "\thelp  : Show this message\n"
      "\tlist  : List currently active devices and codecs.\n"
      "\tregs  : Dump the registers for the specified device ID\n"
      "\tcodec : Dump the internal structure of a codec\n",
      argv[0]);

  return -1;
}

}  // namespace intel_hda
}  // namespace audio

int main(int argc, const char** argv) { return ::audio::intel_hda::main(argc, argv); }
