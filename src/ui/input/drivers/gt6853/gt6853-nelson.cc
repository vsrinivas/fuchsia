// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>

#include "gt6853.h"

namespace {

enum {
  // These values are shared with the bootloader, and must be kept in sync.
  kPanelTypeKdFiti9364 = 1,
  kPanelTypeBoeFiti9364 = 2,
  kPanelTypeInxFiti9364 = 3,
  kPanelTypeKdFiti9365 = 4,
  kPanelTypeBoeFiti9365 = 5,
  kPanelTypeBoeSit7703 = 6,
};

// There are three config files, one for each DDIC. A config file may contain multiple configs; the
// correct one is chosen based on the sensor ID reported by the touch controller.
inline const char* PanelTypeToConfigPath(uint32_t panel_type_id) {
  switch (panel_type_id) {
    case kPanelTypeKdFiti9364:
    case kPanelTypeBoeFiti9364:
    case kPanelTypeInxFiti9364:
      return GT6853_CONFIG_9364_PATH;
    case kPanelTypeKdFiti9365:
    case kPanelTypeBoeFiti9365:
      return GT6853_CONFIG_9365_PATH;
    case kPanelTypeBoeSit7703:
      return GT6853_CONFIG_7703_PATH;
    default:
      return nullptr;
  }
}

inline const char* PanelTypeToNameString(uint32_t panel_type_id) {
  switch (panel_type_id) {
    case kPanelTypeKdFiti9364:
      return "kd_fiti9364";
    case kPanelTypeBoeFiti9364:
      return "boe_fiti9364";
    case kPanelTypeInxFiti9364:
      return "inx_fiti9364";
    case kPanelTypeKdFiti9365:
      return "kd_fiti9365";
    case kPanelTypeBoeFiti9365:
      return "boe_fiti9365";
    case kPanelTypeBoeSit7703:
      return "boe_sit7703";
    default:
      return "unknown";
  }
}

}  // namespace

namespace touch {

zx::result<fuchsia_mem::wire::Range> Gt6853Device::GetConfigFileVmo() {
  size_t actual = 0;
  uint32_t panel_type_id = 0;
  zx_status_t status = DdkGetFragmentMetadata("pdev", DEVICE_METADATA_BOARD_PRIVATE, &panel_type_id,
                                              sizeof(panel_type_id), &actual);
  // The only case to let through is when metadata isn't provided, which could happen after
  // netbooting. All other unexpected conditions are fatal, which should help them be discovered
  // more easily.
  if (status == ZX_ERR_NOT_FOUND) {
    config_status_.Set("skipped, no metadata");
    return zx::ok(fuchsia_mem::wire::Range{});
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get panel type: %d", status);
    return zx::error(status);
  }
  if (actual != sizeof(panel_type_id)) {
    zxlogf(ERROR, "Expected metadata size %zu, got %zu", sizeof(panel_type_id), actual);
    return zx::error(ZX_ERR_INTERNAL);
  }

  panel_type_id_ = root_.CreateInt("panel_type_id", panel_type_id);
  panel_type_ = root_.CreateString("panel_type", PanelTypeToNameString(panel_type_id));

  // The panel should be identified correctly by the bootloader for P2 boards and beyond. This
  // driver isn't be used on boards earlier than P2, so not finding the panel ID is an error.
  const char* config_path = PanelTypeToConfigPath(panel_type_id);
  if (!config_path) {
    zxlogf(ERROR, "Failed to find config for panel type %u", panel_type_id);
    return zx::error(ZX_ERR_INTERNAL);
  }

  // There's a chance we can proceed without a config, but we should always have one on Nelson, so
  // error out if it can't be loaded.
  fuchsia_mem::wire::Range config = {};
  status = load_firmware(parent(), config_path, config.vmo.reset_and_get_address(), &config.size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to load config binary: %d", status);
    return zx::error(status);
  }

  return zx::ok(std::move(config));
}

}  // namespace touch
