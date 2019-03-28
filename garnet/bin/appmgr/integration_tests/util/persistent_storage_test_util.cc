// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <test/appmgr/integration/cpp/fidl.h>
#include <string>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace {

class IsolatedStorageTestUtil
    : public test::appmgr::integration::DataFileReaderWriter {
 public:
  explicit IsolatedStorageTestUtil(
      const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
    outgoing->AddPublicService(bindings_.GetHandler(this));
  }

  void ReadFile(std::string path, ReadFileCallback callback) override {
    std::string contents;
    if (!files::ReadFileToString(files::JoinPath("/data", path), &contents)) {
      callback(nullptr);
      return;
    }
    callback(contents);
  }

  void WriteFile(std::string path, std::string contents,
                 WriteFileCallback callback) override {
    if (!files::WriteFile(files::JoinPath("/data", path), contents.c_str(),
                          contents.length())) {
      callback(ZX_ERR_IO);
      return;
    }
    callback(ZX_OK);
  }

 private:
  fidl::BindingSet<DataFileReaderWriter> bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  IsolatedStorageTestUtil server(context->outgoing());
  loop.Run();
  return 0;
}
