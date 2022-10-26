// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_RUNNER_H_
#define SRC_STORAGE_F2FS_RUNNER_H_

namespace f2fs {

// A wrapper class around a "f2fs" object which additionally manages external IPC connections.
//
// Using this interface, a caller can initialize a f2fs object and access the filesystem hierarchy
// through the ulib/fs Vnode classes, but not modify the internal structure of the filesystem.
//
// The Runner class *has* to be final because it calls PagedVfs::TearDown from
// its destructor which is required to ensure thread-safety at destruction time.
class Runner final : public PlatformVfs {
 public:
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;

  ~Runner() override;

  static zx::result<std::unique_ptr<Runner>> Create(FuchsiaDispatcher dispatcher,
                                                    std::unique_ptr<f2fs::Bcache> bc,
                                                    const MountOptions& options);

  static zx::result<std::unique_ptr<Runner>> CreateRunner(FuchsiaDispatcher dispatcher);

#ifdef __Fuchsia__
  void SetUnmountCallback(fit::closure closure) { on_unmount_ = std::move(closure); }

  // Serves the root directory of the filesystem using |root| as the server-end of an IPC
  // connection.
  zx::result<> ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root);

  // fs::PagedVfs implementation.
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) final;
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() final;
  void OnNoConnections() final;
#endif  // __Fuchsia__

 private:
  explicit Runner(FuchsiaDispatcher dispatcher);

#ifdef __Fuchsia__
  FuchsiaDispatcher dispatcher_;
#endif  // __Fuchsia__
  fit::closure on_unmount_;
  std::unique_ptr<F2fs> f2fs_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_RUNNER_H_
