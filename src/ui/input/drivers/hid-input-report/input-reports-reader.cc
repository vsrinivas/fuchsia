// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-reports-reader.h"

#include "instance.h"

namespace hid_input_report_dev {

void InputReportsReader::ReadInputReports(ReadInputReportsCompleter::Sync completer) {
  instance_->SetWaitingReportsReader(completer.ToAsync());
}

}  // namespace hid_input_report_dev
