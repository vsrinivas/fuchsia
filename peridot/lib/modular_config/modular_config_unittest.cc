// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config.h"

#include <lib/fsl/io/fd.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <peridot/lib/modular_config/modular_config_constants.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/substitute.h>

#include <thread>

namespace modular {
namespace testing {

// Given a pseudo directory, spins up a thread and serves Directory operations
// over it. This utility is useful for making thread-blocking posix calls to the
// given PseudoDir, which needs its owning thread to not be blocked to service
// directory calls.
//
// The directory is accessible using |OpenAt()|.
//
// This class is thread-unsafe.
class PseudoDirServer {
 public:
  // Spins up a thread to serve the given |pseudo_dir| directory calls over.
  // This constructor blocks the current thread until the new thread has
  // initialized.
  //
  // Requires that the calling thread has an async dispatcher.
  PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir)
      : pseudo_dir_(std::move(pseudo_dir)),
        serving_thread_(
            [this](fidl::InterfaceRequest<fuchsia::io::Directory> request) {
              StartThread(std::move(request));
            },
            dir_.NewRequest()) {
    // The thread (owned by |serving_thread_|) kicked off, but lets wait until
    // it's run loop is ready.
    std::unique_lock<std::mutex> lock(ready_mutex_);
    thread_loop_ready_.wait(lock);
  }

  ~PseudoDirServer() {
    FXL_CHECK(thread_loop_);
    // std::thread requires that we join() the thread before it is destroyed.
    thread_loop_->Quit();
    serving_thread_.join();
  }

  // Opens a read-only FD at |path|.  Path must not begin with '/'.
  fxl::UniqueFD OpenAt(std::string path) {
    fuchsia::io::NodePtr node;
    dir_->Open(fuchsia::io::OPEN_RIGHT_READABLE |
                   fuchsia::io::OPEN_FLAG_DESCRIBE,  // flags
               0u,                                   // mode
               path, node.NewRequest());

    return fsl::OpenChannelAsFileDescriptor(node.Unbind().TakeChannel());
  }

 private:
  // This method is the handler for a new thread. It lets the owning thread know
  // that it has started and serves a directory requests. The thread is exited
  // when this object is destroyed.
  void StartThread(fidl::InterfaceRequest<fuchsia::io::Directory> request) {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    thread_loop_ = &loop;

    // The owner's thread is currently blocked waiting for this thread to
    // initialize a valid |thread_loop_|. Notify that thread that it's now safe
    // manipulate this thread's run loop:
    thread_loop_ready_.notify_all();

    pseudo_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE, request.TakeChannel());
    thread_loop_->Run();
    // This thread exits when the owner thread calls thread_loop_->Quit().
  }

  std::unique_ptr<vfs::PseudoDir> pseudo_dir_;
  // The directory connection we that |pseudo_dir| serves over in a differnt
  // thread.
  fuchsia::io::DirectoryPtr dir_;

  // The mutex & condition variable are used by the new thread (owned by
  // |serving_thread_|) to signal to the owning thread that it has started,
  // making it safe to then access |thread_loop_|.
  std::mutex ready_mutex_;
  std::condition_variable thread_loop_ready_;
  async::Loop* thread_loop_ = nullptr;  // serving thread's loop.

  std::thread serving_thread_;
};

// Creates a new directory hosting the /config_override directory structure that
// |ModularConfigReader| expects, with a config file in it containing the given
// |contents|. The new directory is returned.
std::unique_ptr<vfs::PseudoDir> MakeModularConfigDirWithContents(
    std::string contents) {
  auto config_path_split = fxl::SplitStringCopy(
      files::JoinPath(modular_config::kOverriddenConfigDir,
                      modular_config::kStartupConfigFilePath),
      "/", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  // This is the root-level directory of the config namespace (ie. /config or
  // /config_override).
  auto config_dir = std::make_unique<vfs::PseudoDir>();
  auto* last_subdir = config_dir.get();

  // 1. Make each directory in |config_path_split|, except for the last one
  //    which is the config file name.
  for (size_t i = 0; i < config_path_split.size() - 1; i++) {
    auto subdir = std::make_unique<vfs::PseudoDir>();
    auto* subdir_raw = subdir.get();
    last_subdir->AddEntry(config_path_split[i], std::move(subdir));
    last_subdir = subdir_raw;
  }

  // 2. Make the actual config file hanging off of the last directory.
  last_subdir->AddEntry(
      files::GetBaseName(modular_config::kStartupConfigFilePath),
      std::make_unique<vfs::PseudoFile>([contents](std::vector<uint8_t>* out) {
        std::copy(contents.begin(), contents.end(), std::back_inserter(*out));
        return ZX_OK;
      }));

  return config_dir;
}

}  // namespace testing
}  // namespace modular

class ModularConfigReaderTest : public gtest::RealLoopFixture {};

// Test that ModularConfigReader finds and reads the startup.config file given a
// root directory that contains config data.
TEST_F(ModularConfigReaderTest, OverrideConfigDir) {
  constexpr char kSessionShellForTest[] =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  std::string config_contents = fxl::Substitute(R"({
        "basemgr": {
          "session_shells": [
            {
              "url": "$0"
            }
          ]
        }
      })",
                                                kSessionShellForTest);

  modular::testing::PseudoDirServer server(
      modular::testing::MakeModularConfigDirWithContents(config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetBasemgrConfig();

  // Verify that ModularConfigReader parsed the config value we gave it.
  EXPECT_EQ(kSessionShellForTest,
            config.session_shell_map().at(0).config().app_config().url());
}
