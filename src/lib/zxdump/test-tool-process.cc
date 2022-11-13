// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-tool-process.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/stdcompat/string_view.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cstdlib>
#include <filesystem>

#include <gtest/gtest.h>

#include "piped-command.h"

#ifdef __Fuchsia__
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zxdump/task.h>
#include <zircon/syscalls/object.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#else
#include <libgen.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace zxdump::testing {

using namespace std::literals;

std::string GetTmpDir() {
#ifndef __Fuchsia__
  if (const char* tmpdir = getenv("TMPDIR")) {
    std::string dir(tmpdir);
    if (!dir.empty() && dir.back() != '/') {
      dir += '/';
    }
    if (!dir.empty()) {
      return dir;
    }
  }
#endif
  return "/tmp/";
}

std::string ToolPath(std::string tool) {
#ifdef __Fuchsia__
  return std::string("/pkg/bin/") + std::move(tool);
#else
  std::filesystem::path path;
#if defined(__APPLE__)
  uint32_t length = PATH_MAX;
  char self_path[PATH_MAX];
  char self_path_symlink[PATH_MAX];
  _NSGetExecutablePath(self_path_symlink, &length);
  path = dirname(realpath(self_path_symlink, self_path));
#elif defined(__linux__)
  char self_path[PATH_MAX];
  path = dirname(realpath("/proc/self/exe", self_path));
#else
#error unknown platform.
#endif
  return path / tool;
#endif
}

std::string TestToolProcess::FilePathForTool(const TestToolProcess::File& file) const {
  ZX_ASSERT(!tmp_path_.empty());
#ifdef __Fuchsia__
  // The tool process runs in a sandbox where /tmp/ is actually our tmp_path_.
  return "/tmp/" + file.name_;
#else
  // The tool runs in the same filesystem namespace as this test code.
  return tmp_path_ + file.name_;
#endif
}

std::string TestToolProcess::FilePathForRunner(const std::string& name) const {
  ZX_ASSERT(!tmp_path_.empty());
  return tmp_path_ + name;
}

std::string TestToolProcess::FilePathForRunner(const TestToolProcess::File& file) const {
  return FilePathForRunner(file.name_);
}

#ifdef __Fuchsia__

// The tool process runs with a sandbox namespace that has only its own special
// /tmp and /svc.  Its /tmp is mapped to the tmp_path_ subdirectory.  Its /svc
// contains only fuchsia.kernel.RootJob pointing at this fake service that just
// gives the test program's own job instead of the real root job.

template <class Protocol, class Handle>
class SandboxGetServer final : public fidl::WireServer<Protocol> {
 public:
  using typename fidl::WireServer<Protocol>::GetCompleter;

  void Init(zx::unowned<Handle> handle) { handle_ = handle; }

  void Get(typename GetCompleter::Sync& completer) override {
    Handle handle;
    zx_status_t status = handle_->duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
    EXPECT_EQ(status, ZX_OK) << zx_status_get_string(status);
    completer.Reply(std::move(handle));
  }

 private:
  zx::unowned<Handle> handle_;
};

class TestToolProcess::SandboxLoop {
 public:
  void Init(zx::unowned_job job, zx::unowned_resource resource,
            fidl::ClientEnd<fuchsia_io::Directory>& out_svc) {
    loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx_status_t status = loop_->StartThread("TestToolProcess::SandboxLoop");
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

    vfs_.emplace(loop_->dispatcher());
    svc_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();

    AddSvcEntry<fuchsia_kernel::RootJob, &SandboxLoop::root_job_server_>(*job);
    AddSvcEntry<fuchsia_boot::RootResource, &SandboxLoop::root_resource_server_>(*resource);

    auto [svc_client, svc_server] = *fidl::CreateEndpoints<fuchsia_io::Directory>();
    status = vfs_->ServeDirectory(svc_dir_, std::move(svc_server));
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    out_svc = std::move(svc_client);
  }

  ~SandboxLoop() {
    if (loop_) {
      loop_->Shutdown();
    }
  }

 private:
  template <class Protocol, auto Member, class Handle>
  void AddSvcEntry(const Handle& handle) {
    if (handle) {
      (this->*Member).Init(handle.borrow());

      zx_status_t status = svc_dir_->AddEntry(
          fidl::DiscoverableProtocolName<Protocol>,
          fbl::MakeRefCounted<fs::Service>(
              [this](fidl::ServerEnd<Protocol> request) -> zx_status_t {
                fidl::BindServer(loop_->dispatcher(), std::move(request), &(this->*Member));
                return ZX_OK;
              }));
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }
  }

  std::optional<async::Loop> loop_;
  std::optional<fs::SynchronousVfs> vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_dir_;
  SandboxGetServer<fuchsia_kernel::RootJob, zx::job> root_job_server_;
  SandboxGetServer<fuchsia_boot::RootResource, zx::resource> root_resource_server_;
};

// Set the spawn actions to populate the namespace for the tool with only its
// own private /tmp and /svc endpoints.
void TestToolProcess::SandboxCommand(PipedCommand& command) {
  std::vector<fdio_spawn_action_t> actions;

  fbl::unique_fd tmp_fd{open(tmp_path_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  ASSERT_TRUE(tmp_fd) << tmp_path_ << ": " << strerror(errno);
  zx::channel tmp_handle;
  zx_status_t status =
      fdio_get_service_handle(tmp_fd.release(), tmp_handle.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  actions.push_back({.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                     .ns = {
                         .prefix = "/tmp",
                         .handle = tmp_handle.release(),
                     }});

  fidl::ClientEnd<fuchsia_io::Directory> svc;
  sandbox_loop_ = std::make_unique<TestToolProcess::SandboxLoop>();
  ASSERT_NO_FATAL_FAILURE(sandbox_loop_->Init(job_->borrow(), resource_->borrow(), svc));
  actions.push_back({.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                     .ns = {
                         .prefix = "/svc",
                         .handle = svc.TakeChannel().release(),
                     }});

  command.SetSpawnActions(FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE, std::move(actions));
}
#endif  // __Fuchsia__

std::thread SendPipeWorker(fbl::unique_fd fd, std::string contents) {
  return std::thread([fd = std::move(fd), contents = std::move(contents)]() mutable {
    while (!contents.empty()) {
      ssize_t n = write(fd.get(), contents.data(), contents.size());
      if (n < 0) {
        break;
      }
      contents.erase(contents.begin(), contents.begin() + n);
    }
  });
}

std::thread CollectPipeWorker(fbl::unique_fd fd, std::string& result) {
  return std::thread([fd = std::move(fd), &result]() mutable {
    char buf[PIPE_BUF];
    while (true) {
      ssize_t n = read(fd.get(), buf, sizeof(buf));
      if (n <= 0) {
        break;
      }
      result.append(buf, static_cast<size_t>(n));
    }
  });
}

fbl::unique_fd TestToolProcess::File::CreateInput() {
  fbl::unique_fd fd{
      open(owner_->FilePathForRunner(*this).c_str(), O_RDWR | O_CREAT | O_TRUNC | O_EXCL, 0666)};
  EXPECT_TRUE(fd) << owner_->FilePathForRunner(*this).c_str() << ": " << strerror(errno);
  if (fd) {
    EXPECT_EQ(fcntl(fd.get(), F_SETFD, FD_CLOEXEC), 0);
  }
  return fd;
}

fbl::unique_fd TestToolProcess::File::OpenOutput() {
  fbl::unique_fd fd{open(owner_->FilePathForRunner(*this).c_str(), O_RDONLY)};
  if (fd) {
    EXPECT_EQ(fcntl(fd.get(), F_SETFD, FD_CLOEXEC), 0);
  }
  return fd;
}

std::string TestToolProcess::File::OutputContents() {
  std::string contents;
  char buf[BUFSIZ];
  ssize_t nread;
  fbl::unique_fd fd = OpenOutput();
  while ((nread = read(fd.get(), buf, sizeof(buf))) > 0) {
    contents.append(buf, static_cast<size_t>(nread));
  }
  EXPECT_GE(nread, 0) << strerror(errno);
  return contents;
}

TestToolProcess::File TestToolProcess::File::NoFile() {
  auto it = owner_->files_.begin();
  while (it != owner_->files_.end() && std::addressof(*it) != this) {
    ++it;
  }
  EXPECT_NE(it, owner_->files_.end());
  File result = std::move(*this);
  result.owner_->files_.erase(it);
  return result;
}

TestToolProcess::File::~File() = default;

void TestToolProcess::Start(const std::string& tool, const std::vector<std::string>& args) {
  ZX_ASSERT(!tmp_path_.empty());

  PipedCommand command;

  auto redirect = [&](int number, fbl::unique_fd& tool_fd, bool read) {
    if (tool_fd) {
      command.Redirect(number, std::move(tool_fd));
    } else {
      int pipe_fd[2];
      ASSERT_EQ(pipe(pipe_fd), 0) << strerror(errno);
      ASSERT_EQ(fcntl(pipe_fd[0], F_SETFD, FD_CLOEXEC), 0) << strerror(errno);
      ASSERT_EQ(fcntl(pipe_fd[1], F_SETFD, FD_CLOEXEC), 0) << strerror(errno);
      tool_fd.reset(pipe_fd[read ? 0 : 1]);
      command.Redirect(number, fbl::unique_fd{pipe_fd[read ? 1 : 0]});
    }
  };

  redirect(STDIN_FILENO, tool_stdin_, false);
  redirect(STDOUT_FILENO, tool_stdout_, true);
  redirect(STDERR_FILENO, tool_stderr_, true);

#ifdef __Fuchsia__
  ASSERT_NO_FATAL_FAILURE(SandboxCommand(command));
#endif

  auto result = command.Start(ToolPath(tool), args);
  ASSERT_TRUE(result.is_ok()) << result.error_value();

  process_ = std::move(command).process();
}

void TestToolProcess::Finish(int& status) {
#ifdef __Fuchsia__
  ASSERT_TRUE(process_);
  zx_signals_t signals = 0;
  ASSERT_EQ(process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals), ZX_OK);
  ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);
  zx_info_process_t info;
  ASSERT_EQ(process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  status = static_cast<int>(info.return_code);
  process_.reset();
#else
  ASSERT_NE(process_, -1);
  ASSERT_EQ(waitpid(process_, &status, 0), process_);
  status = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
  process_ = -1;
#endif
}

TestToolProcess::TestToolProcess() = default;

void TestToolProcess::Init() {
  tmp_path_ = GetTmpDir() + "tool-tmp.";
  int n = 1;
  while (mkdir((tmp_path_ + std::to_string(n)).c_str(), 0777) < 0) {
    EXPECT_EQ(errno, EEXIST) << strerror(errno);
    ++n;
  }
  tmp_path_ += std::to_string(n) + '/';
  clear_tmp_ = true;
}

void TestToolProcess::Init(std::string_view tmp_path) { tmp_path_ = tmp_path; }

TestToolProcess::~TestToolProcess() {
  bool live = false;
#ifdef __Fuchsia__
  live = process_.is_valid();
#else
  live = process_ != -1;
#endif

  if (live) {
    int status = -1;
    Finish(status);
    EXPECT_EQ(status, 0);
  }

  if (stdin_thread_.joinable()) {
    stdin_thread_.join();
  }
  if (stdout_thread_.joinable()) {
    stdout_thread_.join();
  }
  if (stderr_thread_.joinable()) {
    stderr_thread_.join();
  }

  for (const File& file : files_) {
    std::string path = FilePathForRunner(file);
    EXPECT_EQ(remove(path.c_str()), 0) << file.name() << " as " << path << ": " << strerror(errno);
  }

  if (clear_tmp_) {
    EXPECT_EQ(tmp_path_.back(), '/');
    tmp_path_.resize(tmp_path_.size() - 1);  // Remove trailing slash.
    if (rmdir(tmp_path_.c_str()) != 0) {
      EXPECT_EQ(errno, ENOTEMPTY) << tmp_path_ << ": " << strerror(errno);
      // Emit more complaints with unexpected directory contents if any.
      if (DIR* dir = opendir(tmp_path_.c_str())) {
        while (const dirent* d = readdir(dir)) {
          EXPECT_TRUE(std::string_view(".") == d->d_name || std::string_view("..") == d->d_name)
              << "left in " << tmp_path_ << ": " << d->d_name;
        }
      }
    }
  }
}

void TestToolProcess::SendStdin(std::string contents) {
  ASSERT_TRUE(tool_stdin_);
  ASSERT_FALSE(stdin_thread_.joinable());

  stdin_thread_ = SendPipeWorker(std::move(tool_stdin_), std::move(contents));
}

void TestToolProcess::CollectStdout() {
  ASSERT_TRUE(tool_stdout_);
  ASSERT_FALSE(stdout_thread_.joinable());
  stdout_thread_ = CollectPipeWorker(std::move(tool_stdout_), collected_stdout_);
}

void TestToolProcess::CollectStderr() {
  ASSERT_TRUE(tool_stderr_);
  ASSERT_FALSE(stderr_thread_.joinable());
  stderr_thread_ = CollectPipeWorker(std::move(tool_stderr_), collected_stderr_);
}

std::string TestToolProcess::collected_stdout() {
#ifdef __Fuchsia__
  EXPECT_FALSE(process_);
#else
  EXPECT_EQ(process_, -1);
#endif
  EXPECT_TRUE(stdout_thread_.joinable());
  stdout_thread_.join();
  return std::move(collected_stdout_);
}

std::string TestToolProcess::collected_stderr() {
#ifdef __Fuchsia__
  EXPECT_FALSE(process_);
#else
  EXPECT_EQ(process_, -1);
#endif
  EXPECT_TRUE(stderr_thread_.joinable());
  stderr_thread_.join();
  return std::move(collected_stderr_);
}

TestToolProcess::File& TestToolProcess::MakeFile(std::string_view name, std::string_view suffix) {
  File file;
  file.owner_ = this;
  file.name_ = "test.";
  file.name_ += name;
  file.name_ += '.';
  int n = 1;
  while (std::filesystem::exists(FilePathForRunner(file.name_ + std::to_string(n)) +
                                 std::string(suffix))) {
    ++n;
  }
  file.name_ += std::to_string(n);
  file.name_ += suffix;
  files_.push_back(std::move(file));
  return files_.back();
}

TestToolProcess::File& TestToolProcess::File::ZstdCompress() const {
  File& zstd_file = owner_->MakeFile(name_, kZstdSuffix);
  TestToolProcess zstd_tool;
  zstd_tool.Init(owner_->tmp_path());
  std::vector<std::string> args({
      "-1"s,
      name(),
      "-o"s,
      zstd_file.name(),
  });
  zstd_tool.Start("zstd", args);
  int status;
  zstd_tool.Finish(status);
  EXPECT_EQ(status, EXIT_SUCCESS);
  return zstd_file;
}

TestToolProcess::File& TestToolProcess::File::ZstdDecompress() const {
  ZX_ASSERT(cpp20::ends_with(std::string_view(name_), kZstdSuffix));
  File& plain_file = owner_->MakeFile(name_.substr(0, name_.size() - kZstdSuffix.size()));
  TestToolProcess zstd_tool;
  zstd_tool.Init(owner_->tmp_path());
  std::vector<std::string> args({
      "-d"s,
      name(),
      "-o"s,
      plain_file.name(),
  });
  zstd_tool.Start("zstd"s, args);
  int status;
  zstd_tool.Finish(status);
  EXPECT_EQ(status, EXIT_SUCCESS);
  return plain_file;
}

}  // namespace zxdump::testing
