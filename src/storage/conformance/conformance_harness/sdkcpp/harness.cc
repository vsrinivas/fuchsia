// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <zircon/status.h>

#include <memory>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/syslog/cpp/logger.h"

class TestCasesImpl : public fuchsia::io::test::TestCases {
 public:
  // In the beginning of each method implementation, resetting the |PseudoDir| will
  // also destroy any connections to the old directory. This should be pretty cheap.

  void GetEmptyDirectory(zx::channel directory_request) final {
    empty_dir_case_ = std::make_unique<vfs::PseudoDir>();
    zx_status_t status =
        empty_dir_case_->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                               std::move(directory_request), async_get_default_dispatcher());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving empty directory failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving empty directory";
  }

  void GetDirectoryWithVmoFile(fuchsia::mem::Range buffer, zx::channel directory_request) final {
    vmo_file_case_ = std::make_unique<vfs::PseudoDir>();
    vmo_file_case_->AddEntry("vmo_file", std::make_unique<vfs::VmoFile>(
                                             std::move(buffer.vmo), buffer.offset, buffer.size));
    zx_status_t status =
        vmo_file_case_->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                              std::move(directory_request), async_get_default_dispatcher());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with vmo file failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving directory with vmo file";
  }

 private:
  std::unique_ptr<vfs::PseudoDir> empty_dir_case_;
  std::unique_ptr<vfs::PseudoDir> vmo_file_case_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::InitLogger({"io_conformance_harness_sdkcpp"});

  auto context = sys::ComponentContext::Create();
  sys::ServiceHandler handler;
  fuchsia::io::test::Harness::Handler harness(&handler);

  // Serving the fuchsia.io v1 harness for now.
  TestCasesImpl v1_impl;
  fidl::BindingSet<fuchsia::io::test::TestCases> v1_bindings;
  harness.add_v1(v1_bindings.GetHandler(&v1_impl));

  // Serve an instance of `Harness` service.
  context->outgoing()->AddService<fuchsia::io::test::Harness>(std::move(handler));

  loop.Run();
  return 0;
}
