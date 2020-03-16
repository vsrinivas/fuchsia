// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"

#include <math.h>
#include <stdio.h>
#include <zircon/syscalls.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;

Display::Display(const fhd::Info& info) {
  id_ = info.id;

  auto pixel_format = reinterpret_cast<const int32_t*>(info.pixel_format.data());
  for (unsigned i = 0; i < info.pixel_format.count(); i++) {
    pixel_formats_.push_back(pixel_format[i]);
  }

  auto mode = reinterpret_cast<const fhd::Mode*>(info.modes.data());
  for (unsigned i = 0; i < info.modes.count(); i++) {
    modes_.push_back(mode[i]);
  }

  auto cursors = reinterpret_cast<const fhd::CursorInfo*>(info.cursor_configs.data());
  for (unsigned i = 0; i < info.cursor_configs.count(); i++) {
    cursors_.push_back(cursors[i]);
  }

  manufacturer_name_ = fbl::String(info.manufacturer_name.data());
  monitor_name_ = fbl::String(info.monitor_name.data());
  monitor_serial_ = fbl::String(info.monitor_serial.data());

  horizontal_size_mm_ = info.horizontal_size_mm;
  vertical_size_mm_ = info.vertical_size_mm;
  using_fallback_sizes_ = info.using_fallback_size;
}

void Display::Dump() {
  printf("Display id = %ld\n", id_);
  printf("\tManufacturer name = \"%s\"\n", manufacturer_name_.c_str());
  printf("\tMonitor name = \"%s\"\n", monitor_name_.c_str());
  printf("\tMonitor serial = \"%s\"\n", monitor_serial_.c_str());

  printf("\tSupported pixel formats:\n");
  for (unsigned i = 0; i < pixel_formats_.size(); i++) {
    printf("\t\t%d\t: %08x\n", i, pixel_formats_[i]);
  }

  printf("\n\tSupported display modes:\n");
  for (unsigned i = 0; i < modes_.size(); i++) {
    printf("\t\t%d\t: %dx%d\t%d.%02d\n", i, modes_[i].horizontal_resolution,
           modes_[i].vertical_resolution, modes_[i].refresh_rate_e2 / 100,
           modes_[i].refresh_rate_e2 % 100);
  }

  printf("\n\tSupported cursor modes:\n");
  for (unsigned i = 0; i < cursors_.size(); i++) {
    printf("\t\t%d\t: %dx%d\t%08x\n", i, cursors_[i].width, cursors_[i].height,
           cursors_[i].pixel_format);
  }

  printf("\n\t%s Physical dimension in millimeters:\n",
         using_fallback_sizes_ ? "[Best Guess / Fallback]" : "");
  printf("\t\tHorizontal size = %d mm\n", horizontal_size_mm_);
  printf("\t\tVertical size = %d mm\n", vertical_size_mm_);
  printf("\n");
}

void Display::Init(fhd::Controller::SyncClient* dc) {
  if (mode_idx_ != 0) {
    ZX_ASSERT(dc->SetDisplayMode(id_, modes_[mode_idx_]).ok());
  }

  if (grayscale_) {
    ::fidl::Array<float, 3> preoffsets = {nanf("pre"), 0, 0};
    ::fidl::Array<float, 3> postoffsets = {nanf("post"), 0, 0};
    ::fidl::Array<float, 9> grayscale = {
        .2126f, .7152f, .0722f, .2126f, .7152f, .0722f, .2126f, .7152f, .0722f,
    };
    ZX_ASSERT(dc->SetDisplayColorConversion(id_, preoffsets, grayscale, postoffsets).ok());
  }
}
