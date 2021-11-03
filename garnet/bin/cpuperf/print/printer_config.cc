// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printer_config.h"

namespace cpuperf {

RawPrinter::Config PrinterConfig::ToRawPrinterConfig() const {
  RawPrinter::Config config;
  config.output_file_name = output_file_name;
  return config;
}

}  // namespace cpuperf
