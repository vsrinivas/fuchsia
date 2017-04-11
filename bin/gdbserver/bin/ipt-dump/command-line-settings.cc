// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command-line-settings.h"

namespace intel_processor_trace {

RawPrinter::Config CommandLineSettings::ToRawPrinterConfig() const {
  RawPrinter::Config config;
  config.output_file_name = output_file_name;
  return config;
};

}  // namespace intel_processor_trace
