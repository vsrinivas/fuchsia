// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <fbl/unique_fd.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/memory/unique_object.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

using ControllerPtr = fuchsia::developer::tiles::ControllerSyncPtr;

struct UniqueDIRTraits {
  static DIR* InvalidValue() { return nullptr; }
  static bool IsValid(DIR* value) { return value != nullptr; }
  static void Free(DIR* dir) { closedir(dir); }
};

using UniqueDIR = fxl::UniqueObject<DIR*, UniqueDIRTraits>;

void Usage() {
  printf(
      "Usage: tiles_ctl [--flatland] <command>\n"
      "  Supported commands:\n"
      "    start\n"
      "    add [--disable-focus] <url> [<args>...]\n"
      "    remove <key>\n"
      "    list\n"
      "    quit\n");
}

std::string FirstNumericEntryInDir(const UniqueDIR& dir) {
  for (struct dirent* de = readdir(dir.get()); de != nullptr; de = readdir(dir.get())) {
    char* name = de->d_name;
    if (!name[0] && name[0] == '.')
      continue;
    if (name[0] >= '0' && name[0] <= '9') {
      return std::string(name);
    }
  }
  return "";
}

ControllerPtr FindTilesService(bool use_flatland) {
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
  std::string tiles_name =
      sys_realm_entry + (use_flatland ? "/c/tiles-flatland.cmx" : "/c/tiles.cmx/");
  fbl::unique_fd tile_component(
      openat(dirfd(sys.get()), tiles_name.c_str(), O_DIRECTORY | O_RDONLY));
  if (!tile_component.is_valid()) {
    if (use_flatland) {
      fprintf(stderr,
              "Couldn't find flatline tiles component in realm\n"
              "To start a new instance of tiles, run 'tiles_ctl --flatland start'\n");

    } else {
      fprintf(stderr,
              "Couldn't find tiles component in realm\n"
              "To start a new instance of tiles, run 'tiles_ctl start'\n");
    }
    return {};
  }

  UniqueDIR tile_component_dir(fdopendir(tile_component.get()));
  std::string tile_realm_entry = FirstNumericEntryInDir(tile_component_dir);
  if (tile_realm_entry == "") {
    fprintf(stderr, "Couldn't find entry in tile component\n");
    return {};
  }
  std::string svc_name = tile_realm_entry + "/out/svc";
  fbl::unique_fd tile_svc(
      openat(dirfd(tile_component_dir.get()), svc_name.c_str(), O_DIRECTORY | O_RDONLY));
  if (!tile_svc.is_valid()) {
    fprintf(stderr, "Couldn't open tile service directory\n");
    return {};
  }

  zx::channel svc_channel = fsl::CloneChannelFromFileDescriptor(tile_svc.get());
  ControllerPtr tiles;
  zx_status_t st =
      fdio_service_connect_at(svc_channel.release(), fuchsia::developer::tiles::Controller::Name_,
                              tiles.NewRequest().TakeChannel().release());
  if (st != ZX_OK) {
    fprintf(stderr, "Couldn't connect to tile service: %d\n", st);
    return {};
  }
  return tiles;
}

bool Start(bool use_flatland) {
  auto services = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::sys::LaunchInfo launch_info;
  if (use_flatland) {
    launch_info.url = "fuchsia-pkg://fuchsia.com/tiles#meta/tiles-flatland.cmx";
  } else {
    launch_info.url = "fuchsia-pkg://fuchsia.com/tiles#meta/tiles.cmx";
  }

  fuchsia::sys::LauncherSyncPtr launcher;
  services->Connect(launcher.NewRequest());

  return launcher->CreateComponent(std::move(launch_info), {}) == ZX_OK;
}

bool Add(bool use_flatland, std::string url, bool allow_focus, std::vector<std::string> args) {
  auto tiles = FindTilesService(use_flatland);
  if (!tiles)
    return false;
  uint32_t key = 0;
  std::vector<std::string> arguments;
  for (const auto& it : args) {
    arguments.push_back(it);
  }
  if (tiles->AddTileFromURL(url, allow_focus, std::move(arguments), &key) != ZX_OK)
    return false;
  printf("Tile added with key %u\n", key);
  return true;
}

bool Remove(bool use_flatland, uint32_t key) {
  auto tiles = FindTilesService(use_flatland);
  if (!tiles)
    return false;
  return tiles->RemoveTile(key) == ZX_OK;
}

bool List(bool use_flatland) {
  auto tiles = FindTilesService(use_flatland);
  if (!tiles)
    return false;

  std::vector<uint32_t> keys;
  std::vector<std::string> urls;
  std::vector<fuchsia::ui::gfx::vec3> sizes;
  std::vector<bool> focusabilities;

  if (tiles->ListTiles(&keys, &urls, &sizes, &focusabilities) != ZX_OK)
    return false;

  printf("Found %lu tiles:\n", keys.size());
  for (size_t i = 0u; i < keys.size(); ++i) {
    printf("Tile key %u url %s size %.1fx%.1fx%.1f%s\n", keys.at(i), (urls.at(i)).c_str(),
           sizes.at(i).x, sizes.at(i).y, sizes.at(i).z,
           focusabilities.at(i) ? " (unfocusable)" : "");
  }
  return true;
}

bool Quit(bool use_flatland) {
  auto tiles = FindTilesService(use_flatland);
  if (!tiles)
    return false;

  return tiles->Quit() == ZX_OK;
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  const auto& positional_args = command_line.positional_args();

  if (positional_args.empty()) {
    Usage();
    return 1;
  }

  const std::string use_flatland_string =
      command_line.GetOptionValueWithDefault("flatland", "false");
  if ((use_flatland_string != "") && (use_flatland_string != "true") &&
      (use_flatland_string != "false")) {
    Usage();
    return 1;
  }
  const bool use_flatland = (use_flatland_string != "false");

  const auto& cmd = positional_args[0];

  if (cmd == "start") {
    if (!Start(use_flatland)) {
      return 1;
    }
  } else if (cmd == "add") {
    if (positional_args.size() < 2) {
      Usage();
      return 1;
    }

    bool allow_focus = positional_args[1] != "--disable-focus";
    if (!allow_focus && positional_args.size() < 3) {
      Usage();
      return 1;
    }
    int adjust = allow_focus ? 0 : 1;
    auto url = positional_args[1 + adjust];
    std::vector<std::string> component_args{std::next(positional_args.begin(), 2 + adjust),
                                            positional_args.end()};
    if (!Add(use_flatland, url, allow_focus, component_args)) {
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
    if (!Remove(use_flatland, key)) {
      return 1;
    }
  } else if (cmd == "list") {
    if (!List(use_flatland))
      return 1;
  } else if (cmd == "quit") {
    if (!Quit(use_flatland)) {
      return 1;
    }
  } else {
    Usage();
    return 1;
  }

  return 0;
}
