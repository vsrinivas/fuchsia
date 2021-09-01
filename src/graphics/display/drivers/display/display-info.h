// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_DISPLAY_INFO_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_DISPLAY_INFO_H_

// NOTE: this header is included first on purpose to avoid clashing with
// Banjo-generated ones below.
// clang-format off
#include <fidl/fuchsia.hardware.display/cpp/wire.h>

#include <fuchsia/hardware/audiotypes/c/banjo.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
// clang-format on
#include <lib/ddk/debug.h>
#include <lib/edid/edid.h>
#include <lib/fidl-utils/bind.h>
#include <lib/inspect/cpp/inspect.h>

#include <functional>
#include <memory>

#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>

#include "id-map.h"

namespace display {

class DisplayInfo : public IdMappable<fbl::RefPtr<DisplayInfo>>,
                    public fbl::RefCounted<DisplayInfo> {
 public:
  static zx::status<fbl::RefPtr<DisplayInfo>> Create(
      const added_display_args_t& info, ddk::I2cImplProtocolClient* i2c);

  // Should be called after init_done is set to true.
  void InitializeInspect(inspect::Node* parent_node);

  void GetPhysicalDimensions(uint32_t* horizontal_size_mm, uint32_t* vertical_size_mm) {
    if (edid.has_value()) {
      *horizontal_size_mm = edid->base.horizontal_size_mm();
      *vertical_size_mm = edid->base.vertical_size_mm();
    } else {
      *horizontal_size_mm = *vertical_size_mm = 0;
    }
  }

  // Get human readable identifiers for this display. Strings will only live as
  // long as the containing DisplayInfo, callers should copy these if they want
  // to retain them longer.
  void GetIdentifiers(const char** manufacturer_name,
                      const char** monitor_name,
                      const char** monitor_serial) {
    if (edid.has_value()) {
      *manufacturer_name = edid->base.manufacturer_name();
      if (!strcmp("", *manufacturer_name)) {
        *manufacturer_name = edid->base.manufacturer_id();
      }
      *monitor_name = edid->base.monitor_name();
      *monitor_serial = edid->base.monitor_serial();
    } else {
      *manufacturer_name = *monitor_name = *monitor_serial = "";
    }
  }

  struct Edid {
    edid::Edid base;
    fbl::Vector<edid::timing_params_t> timings;
    fbl::Vector<audio_types_audio_stream_format_range_t> audio;
  };

  std::optional<Edid> edid;

  // This field has no meaning if EDID information is available.
  display_params_t params;

  fbl::Array<zx_pixel_format_t> pixel_formats;
  fbl::Array<cursor_info_t> cursor_infos;

  // Flag indicating that the display is ready to be published to clients.
  bool init_done = false;

  // A list of all images which have been sent to display driver. For multiple
  // images which are displayed at the same time, images with a lower z-order
  // occur first.
  list_node_t images = LIST_INITIAL_VALUE(images);
  // The number of layers in the applied configuration which are important for vsync (i.e.
  // that have images).
  uint32_t vsync_layer_count;

  // Set when a layer change occurs on this display and cleared in vsync
  // when the new layers are all active.
  bool pending_layer_change;
  // Flag indicating that a new configuration was delayed during a layer change
  // and should be reapplied after the layer change completes.
  bool delayed_apply;

  // True when we're in the process of switching between display clients.
  bool switching_client = false;

 private:
  DisplayInfo() = default;
  void PopulateDisplayAudio();
  inspect::Node node;
  inspect::ValueList properties;
};


}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_DISPLAY_INFO_H_
