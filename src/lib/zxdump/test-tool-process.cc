// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-tool-process.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "piped-command.h"

#ifdef __Fuchsia__
#include <zircon/syscalls/object.h>
#else
#include <libgen.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace zxdump::testing {

std::string GetTmpDir() {
  return "/tmp/";  // TODO(mcgrathr): not always right for non-Fuchsia
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

std::string FilePathForTool(const TestToolProcess::File& file) { return GetTmpDir() + file.name_; }

// TODO(mcgrathr): Run the child in a different sandbox that only sees the
// controlled files in its tmp.
std::string FilePathForRunner(const std::string& name) { return GetTmpDir() + name; }

std::string FilePathForRunner(const TestToolProcess::File& file) {
  return FilePathForRunner(file.name_);
}

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

std::string TestToolProcess::File::name() const { return FilePathForTool(*this); }

fbl::unique_fd TestToolProcess::File::CreateInput() {
  fbl::unique_fd fd{
      open(FilePathForRunner(*this).c_str(), O_RDWR | O_CREAT | O_TRUNC | O_EXCL, 0666)};
  EXPECT_TRUE(fd) << FilePathForRunner(*this).c_str() << ": " << strerror(errno);
  if (fd) {
    EXPECT_EQ(fcntl(fd.get(), F_SETFD, FD_CLOEXEC), 0);
  }
  return fd;
}

fbl::unique_fd TestToolProcess::File::OpenOutput() {
  fbl::unique_fd fd{open(FilePathForRunner(*this).c_str(), O_RDONLY)};
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

TestToolProcess::File::~File() = default;

void TestToolProcess::Start(const std::string& tool, const std::vector<std::string>& args) {
  PipedCommand command;

  auto redirect = [&](int number, fbl::unique_fd& tool_fd, bool read) {
    if (!tool_fd) {
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
    remove(FilePathForRunner(file).c_str());
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

TestToolProcess::File& TestToolProcess::MakeFile(std::string_view name) {
  File file;
  file.name_ = "test.";
  file.name_ += name;
  file.name_ += '.';
  int n = 1;
  while (std::filesystem::exists(FilePathForRunner(file.name_ + std::to_string(n)))) {
    ++n;
  }
  file.name_ += std::to_string(n);
  files_.push_back(std::move(file));
  return files_.back();
}

}  // namespace zxdump::testing
