// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/media_retriever.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>

namespace root_presenter {

namespace {

constexpr char CONFIG_DATA_PATH[] = "/config/data/";
constexpr char FACTORY_RESET_SOUND_PATH[] = "chirp-start-tone.wav";

}  // namespace

MediaRetriever::~MediaRetriever() {}

MediaRetriever::ResetSoundResult MediaRetriever::GetResetSound() {
  fidl::InterfaceHandle<fuchsia::io::File> sound_file;
  zx_status_t open_status;
  {
    std::string path(CONFIG_DATA_PATH);
    path += FACTORY_RESET_SOUND_PATH;
    open_status = fdio_open(path.c_str(), ZX_FS_RIGHT_READABLE,
                            sound_file.NewRequest().TakeChannel().release());
  }

  if (ZX_OK != open_status) {
    return fpromise::error(open_status);
  }

  return fpromise::ok(std::move(sound_file));
}

}  // namespace root_presenter
