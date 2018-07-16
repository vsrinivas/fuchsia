// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <lib/fdio/util.h>

#include "fuchsia/developer/tiles/cpp/fidl.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/memory/unique_object.h"
#include "lib/fxl/strings/string_number_conversions.h"

using TilesPtr = fuchsia::developer::tiles::TilesSyncPtr;

struct UniqueDIRTraits {
  static DIR* InvalidValue() { return nullptr; }
  static bool IsValid(DIR* value) { return value != nullptr; }
  static void Free(DIR* dir) { closedir(dir); }
};

using UniqueDIR = fxl::UniqueObject<DIR*, UniqueDIRTraits>;

void Usage() {
  printf(
      "Usage: tiles_ctl <command>\n"
      "  Supported commands:\n"
      "    add <url> [<args>...]\n"
      "    remove <key>\n"
      "    list\n");
}

std::string FirstNumericEntryInDir(const UniqueDIR& dir) {
  for (struct dirent* de = readdir(dir.get()); de != nullptr;
       de = readdir(dir.get())) {
    char* name = de->d_name;
    if (!name[0] && name[0] == '.')
      continue;
    if (name[0] >= '0' && name[0] <= '9') {
      return std::string(name);
    }
  }
  return "";
}

TilesPtr FindTiles() {
  std::string sys_realm_entry;
  UniqueDIR sys(opendir("/hub/r/sys/"));
  if (sys.is_valid()) {
    sys_realm_entry = FirstNumericEntryInDir(sys);
    if (sys_realm_entry == "") {
      fprintf(stderr, "Couldn't find entry in system realm\n");
      return {};
    }
  } else {
    sys.reset(opendir("/"));
    sys_realm_entry = "hub";
  }
  std::string tiles_name = sys_realm_entry + "/c/tiles/";
  fxl::UniqueFD tile_component(
      openat(dirfd(sys.get()), tiles_name.c_str(), O_DIRECTORY | O_RDONLY));
  if (!tile_component.is_valid()) {
    fprintf(stderr,
            "Couldn't find tiles component in realm\n"
            "To start tiles: run -d tiles\n");
    return {};
  }

  UniqueDIR tile_component_dir(fdopendir(tile_component.get()));
  std::string tile_realm_entry = FirstNumericEntryInDir(tile_component_dir);
  if (tile_realm_entry == "") {
    fprintf(stderr, "Couldn't find entry in tile component\n");
    return {};
  }
  std::string svc_name = tile_realm_entry + "/out/public";
  fxl::UniqueFD tile_svc(openat(dirfd(tile_component_dir.get()),
                                svc_name.c_str(), O_DIRECTORY | O_RDONLY));
  if (!tile_svc.is_valid()) {
    fprintf(stderr, "Couldn't open tile service directory\n");
    return {};
  }

  zx::channel svc_channel = fsl::CloneChannelFromFileDescriptor(tile_svc.get());
  TilesPtr tiles;
  zx_status_t st = fdio_service_connect_at(
      svc_channel.release(), fuchsia::developer::tiles::Tiles::Name_,
      tiles.NewRequest().TakeChannel().get());
  if (st != ZX_OK) {
    fprintf(stderr, "Couldn't connect to tile service: %d\n", st);
    return {};
  }
  return tiles;
}

bool Add(std::string url, std::vector<std::string> args) {
  auto tiles = FindTiles();
  if (!tiles)
    return false;
  uint32_t key = 0;
  fidl::VectorPtr<fidl::StringPtr> arguments;
  for (const auto& it : args) {
    arguments.push_back(it);
  }
  if (tiles->AddTileFromURL(url, std::move(arguments), &key) != ZX_OK)
    return false;
  printf("Tile added with key %u\n", key);
  return true;
}

bool Remove(uint32_t key) {
  auto tiles = FindTiles();
  if (!tiles)
    return false;
  return tiles->RemoveTile(key) == ZX_OK;
}

bool List() {
  auto tiles = FindTiles();
  if (!tiles)
    return false;

  fidl::VectorPtr<uint32_t> keys;
  fidl::VectorPtr<::fidl::StringPtr> urls;
  fidl::VectorPtr<::fuchsia::math::SizeF> sizes;

  if (tiles->ListTiles(&keys, &urls, &sizes) != ZX_OK)
    return false;

  printf("Found %lu tiles:\n", keys->size());
  for (size_t i = 0u; i < keys->size(); ++i) {
    printf("Tile key %u url %s size %.1fx%.1f\n", keys->at(i),
           (*urls->at(i)).c_str(), sizes->at(i).width, sizes->at(i).height);
  }
  return true;
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  const auto& positional_args = command_line.positional_args();

  if (positional_args.empty()) {
    Usage();
    return 1;
  }

  const auto& cmd = positional_args[0];

  if (cmd == "add") {
    if (positional_args.size() < 2) {
      Usage();
      return 1;
    }
    auto url = positional_args[1];
    std::vector<std::string> component_args{
        std::next(positional_args.begin(), 2), positional_args.end()};
    if (!Add(url, component_args)) {
      return 1;
    }
  } else if (cmd == "remove") {
    if (positional_args.size() < 2) {
      Usage();
      return 1;
    }
    uint32_t key;
    if (!fxl::StringToNumberWithError(positional_args[1], &key)) {
      Usage();
      return 1;
    }
    if (!Remove(key)) {
      return 1;
    }
  } else if (cmd == "list") {
    if (!List())
      return 1;
  } else {
    Usage();
    return 1;
  }

  return 0;
}
