// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FS_MANAGER_H_
#define SRC_STORAGE_FSHOST_FS_MANAGER_H_

#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <iterator>
#include <map>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/storage/fshost/fdio.h"
#include "src/storage/fshost/fshost-boot-args.h"
#include "src/storage/fshost/fshost_config.h"
#include "src/storage/fshost/inspect-manager.h"
#include "src/storage/fshost/metrics.h"
#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/vnode_dir.h"

namespace fshost {

class BlockWatcher;

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
 public:
  explicit FsManager(std::shared_ptr<FshostBootArgs> boot_args,
                     std::unique_ptr<FsHostMetrics> metrics);
  ~FsManager();

  zx_status_t Initialize(fidl::ServerEnd<fuchsia_io::Directory> dir_request,
                         fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request,
                         const fshost_config::Config& config, BlockWatcher& watcher);

  // MountPoint is a possible location that a filesystem can be installed at.
  enum class MountPoint {
    kData,
    kFactory,
    kDurable,
  };

  // Returns the fully qualified for the given mount point.
  static const char* MountPointPath(MountPoint);

  struct MountPointEndpoints {
    fidl::UnownedClientEnd<fuchsia_io::Directory> export_root;
    fidl::ServerEnd<fuchsia_io::Directory> server_end;
  };
  // Takes the server end of the specified mount point to send to a hosted filesystem. This channel
  // pair will have been collecting queued requests since fshost was started.
  //
  // This can only be called once per mount point, any calls beyond that will return std::nullopt.
  std::optional<MountPointEndpoints> TakeMountPointServerEnd(MountPoint point);

  // Registers the device path for the given mount point.
  void RegisterDevicePath(MountPoint point, std::string_view device_path);

  // Takes the server end reserved for pkgfs. This channel pair will have been collecting queued
  // requests since fshost was started.
  //
  // This function can only be called once, any calls beyond that will return std::nullopt.
  std::optional<fidl::ServerEnd<fuchsia_io::Directory>> TakePkgfsServerEnd();

  // Creates a connection to the /fs dir in the outgoing directory.
  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> GetFsDir();

  // Asynchronously shut down all the filesystems managed by fshost and then signal the main thread
  // to exit. Calls |callback| when complete. The Shutdown process would block until
  // ReadyForShutdown is called.
  void Shutdown(fit::function<void(zx_status_t)> callback);

  // Returns a pointer to the |FsHostMetrics| instance.
  FsHostMetrics* mutable_metrics() { return metrics_.get(); }

  InspectManager& inspect_manager() { return inspect_; }

  // Flushes FsHostMetrics to cobalt.
  void FlushMetrics();

  std::shared_ptr<FshostBootArgs> boot_args() { return boot_args_; }

  bool IsShutdown();
  void WaitForShutdown();
  void ReadyForShutdown();

  // Creates a new subdirectory in the fshost diagnostics directory by the name of
  // |diagnostics_dir_name|, which forwards the diagnostics dir exposed in the export root directory
  // of the given filesystem previously installed via |InstallFs()| at |point|.
  zx_status_t ForwardFsDiagnosticsDirectory(MountPoint point,
                                            std::string_view diagnostics_dir_name);

  // Creates a new subdirectory in the fshost svc directory by the name of
  // |service_name|, which forwards the service by the same name exposed in the outgoing service
  // directory of the given filesystem previously installed via |InstallFs()| at |point|.
  zx_status_t ForwardFsService(MountPoint point, const char* service_name);

  // Disables reporting.  Future calls to |FileReport| will be NOPs.
  void DisableCrashReporting() { file_crash_report_ = false; }

  // Note that additional reasons should be added sparingly, and only in cases where the data is
  // useful and it would be difficult to debug the issue otherwise.
  enum ReportReason {
    // Unable to mount due to fsck failure.
    kFsckFailure,
  };

  // Files a synthetic crash report.  This is done in the background on a new thread, so never
  // blocks. Note that there is no indication if the reporting fails.
  void FileReport(fs_management::DiskFormat format, ReportReason reason) const;

  zx_status_t AttachMount(std::string_view device_path,
                          fidl::ClientEnd<fuchsia_io::Directory> export_root,
                          std::string_view name);

  zx_status_t DetachMount(std::string_view name);

  zx::status<std::string> GetDevicePath(uint64_t fs_id);

  // Returns the filesystem root for the given mount point.
  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> GetRoot(MountPoint point);

 private:
  class MountedFilesystem {
   public:
    struct Compare {
      using is_transparent = void;
      bool operator()(const std::unique_ptr<MountedFilesystem>& a,
                      const std::unique_ptr<MountedFilesystem>& b) const {
        return a->name_ < b->name_;
      }

      bool operator()(const std::unique_ptr<MountedFilesystem>& a, std::string_view b) const {
        return a->name_ < b;
      }

      bool operator()(std::string_view a, const std::unique_ptr<MountedFilesystem>& b) const {
        return a < b->name_;
      }
    };

    MountedFilesystem(std::string_view name, fidl::ClientEnd<fuchsia_io::Directory> export_root,
                      uint64_t fs_id)
        : name_(name), export_root_(std::move(export_root)), fs_id_(fs_id) {}
    ~MountedFilesystem();

    uint64_t fs_id() const { return fs_id_; }

   private:
    std::string name_;
    fidl::ClientEnd<fuchsia_io::Directory> export_root_;
    uint64_t fs_id_;
  };

  // Represents a channel pair for an expected filesystem instance. When fshost starts, it creates
  // these channel pairs and exposes them in its outgoing directory. They queue filesystem
  // requests, which are then serviced when the server_end is provided to the filesystem on
  // startup.
  //
  // When a filesystem to be started, the server_end is taken with TakeMountPointServerEnd and
  // replaced with std::nullopt. The server_end is then passed to the filesystem.
  struct MountNode {
    fidl::ClientEnd<fuchsia_io::Directory> export_root;
    std::optional<fidl::ServerEnd<fuchsia_io::Directory>> server_end;
  };
  std::map<MountPoint, MountNode> mount_nodes_;

  // pkgfs is special - it doesn't serve a compliant filesystem export root, and it doesn't have a
  // device path. Because of this we keep track of it separately.
  std::optional<fidl::ServerEnd<fuchsia_io::Directory>> pkgfs_server_end_;

  // The memfs which serves the /tmp directory.
  std::unique_ptr<memfs::Memfs> tmp_;

  std::unique_ptr<async::Loop> global_loop_;
  fs::ManagedVfs vfs_;

  // Keeps a collection of metrics being track at the FsHost level.
  std::unique_ptr<FsHostMetrics> metrics_;

  // Serves inspect data.
  InspectManager inspect_;

  // Used to lookup configuration options stored in fuchsia.boot.Arguments
  std::shared_ptr<fshost::FshostBootArgs> boot_args_;

  fbl::RefPtr<fs::PseudoDir> svc_dir_;
  fbl::RefPtr<fs::PseudoDir> fs_dir_;
  fbl::RefPtr<fs::PseudoDir> mnt_dir_;

  // The diagnostics directory for the fshost inspect tree.
  // Each filesystem gets a subdirectory to host their own inspect tree.
  // Archivist will parse all the inspect trees found in this directory tree.
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;

  std::mutex shutdown_lock_;
  bool shutdown_called_ __TA_GUARDED(shutdown_lock_) = false;
  sync_completion_t shutdown_;
  sync_completion_t ready_for_shutdown_;

  bool file_crash_report_ = true;

  std::set<std::unique_ptr<MountedFilesystem>, MountedFilesystem::Compare> mounted_filesystems_;
  std::mutex device_paths_lock_;
  std::unordered_map<uint64_t, std::string> device_paths_ __TA_GUARDED(device_paths_lock_);
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FS_MANAGER_H_
