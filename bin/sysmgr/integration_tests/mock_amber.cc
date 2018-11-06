// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>
#include <fuchsia/amber/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"

namespace {

// Mock out amber which is required when auto_update_packages=true.
// We don't want to depend on the real amber because that would make for a
// non-hermetic test.
class AmberControlMock : public fuchsia::amber::Control {
 public:
  AmberControlMock()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

  void GetUpdateComplete(
      ::fidl::StringPtr name, ::fidl::StringPtr version,
      ::fidl::StringPtr merkle,
      GetUpdateCompleteCallback callback) override {
    zx::channel h1, h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK) {
      callback(zx::channel());
      return;
    }
    callback(std::move(h2));
    static const char kMessage[] = "Hello world";
    if (h1.write(0, kMessage, sizeof(kMessage), nullptr, 0) != ZX_OK) {
      callback(zx::channel());
      return;
    }
    update_channels_.push_back(std::move(h1));
  }

  //
  // Required stubs.
  //

  void DoTest(int32_t input, DoTestCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void AddSrc(fuchsia::amber::SourceConfig source,
              AddSrcCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void RemoveSrc(::fidl::StringPtr id, RemoveSrcCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void ListSrcs(ListSrcsCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void GetBlob(::fidl::StringPtr merkle) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void PackagesActivated(::fidl::VectorPtr<::fidl::StringPtr> merkle) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void CheckForSystemUpdate(
      fuchsia::amber::Control::CheckForSystemUpdateCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void Login(::fidl::StringPtr sourceId, LoginCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void SetSrcEnabled(
      ::fidl::StringPtr id, bool enabled,
      fuchsia::amber::Control::SetSrcEnabledCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void GC() override {
    FXL_LOG(FATAL) << "not implemented";
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::amber::Control> bindings_;
  std::vector<zx::channel> update_channels_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  AmberControlMock service;
  loop.Run();
  return 0;
}
