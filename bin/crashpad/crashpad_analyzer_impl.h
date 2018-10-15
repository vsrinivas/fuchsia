// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
#define GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace fuchsia {
namespace crash {

class CrashpadAnalyzerImpl : public Analyzer {
 public:
  void Analyze(zx::process process, zx::thread thread, zx::port exception_port,
               AnalyzeCallback callback) override;

  void Process(fuchsia::mem::Buffer crashlog,
               ProcessCallback callback) override;
};

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
