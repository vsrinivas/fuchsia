// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_
#define SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_

#include <list>
#include <string>
#include <string_view>
#include <thread>
#ifdef __Fuchsia__
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#endif

#include <fbl/unique_fd.h>

namespace zxdump {

// Forward declaration.
class PipedCommand;

namespace testing {

// This manages a command-line tool process to be run in a sandbox (on Fuchsia)
// or from the build directory (on other hosts) with specified input and output
// files and fully captured stdin/stdout/stderr.
class TestToolProcess {
 public:
  class File {
   public:
    static constexpr std::string_view kZstdSuffix = ".zst";

    File() = default;
    File(const File&) = delete;
    File(File&&) = default;

    ~File();

    const std::string& tmp_path() const { return owner_->tmp_path(); }

    // Return the name of the file as seen by the tool run by Start.
    // This is used in composing the arguments to pass to Start.
    std::string name() const { return owner_->FilePathForTool(*this); }

    // Create the file so it can be written and used as input to the tool.
    // This is used before Start, with name() used to compose the arguments.
    fbl::unique_fd CreateInput();

    // Read the file after it's been written by the tool.
    // This is used  after Finish.
    fbl::unique_fd OpenOutput();

    // Uses OpenOutput to read the whole file.
    std::string OutputContents();

    // Don't expect this file to be created.
    File NoFile();

    // This immediately takes the existing file already written and runs
    // the zstd tool to compress it into a file of the same name + ".zst".
    File& ZstdCompress() const;

    // This immediately takes the existing file ending in ".zst" and uses
    // the zstd tool to decompress it into a file of the same name - ".zst".
    File& ZstdDecompress() const;

   private:
    TestToolProcess* owner_ = nullptr;
    friend TestToolProcess;

    std::string name_;
  };

  TestToolProcess();
  TestToolProcess(const TestToolProcess&) = delete;
  TestToolProcess(TestToolProcess&&) = default;

  ~TestToolProcess();

  // This creates a new isolated tmp directory.
  void Init();

  // This uses an existing tmp directory shared with another TestToolProcess.
  void Init(std::string_view tmp_path);

  // Return a file name that can be passed to the tool via its Start arguments.
  // The file name will include the given name string for debugging purposes,
  // but will be unique among all MakeFileName calls on this TestToolProcess.
  // It will always end with the precise suffix given, if any.  Whether these
  // are input files the test code writes for the tool to read, or output files
  // the tool writes and the test code checks afterwards, they will be cleaned
  // up when the TestToolProcess object is destroyed.
  File& MakeFile(std::string_view name, std::string_view suffix = "");

  // Return the name to access the file in this test program.
  std::string FilePathForRunner(const File& file) const;

  // Return the name to access the file in the child tool program.
  std::string FilePathForTool(const File& file) const;

  // Start the tool running.  This throws gtest assertions for problems.  The
  // tool name is "gcore" or the like, and is found in the appropriate place.
  void Start(const std::string& tool, const std::vector<std::string>& args);

  // Wait for the tool to finish and yield what it passed to exit().  This uses
  // a negative synthetic exit code if the tool process crashed and throws
  // gtest assertions for unexpected problems aside from the process dying.
  void Finish(int& exit_status);

  // These give separate pipe ends to write to the tool's stdin or read from
  // its stdout or stderr.  After Start, these can be reset to close the pipe
  // so the tool under test sees EOF or EPIPE.  Before Start, these can be set
  // to redirect the tool to use another fd; then Start will not make a pipe.
  fbl::unique_fd& tool_stdin() { return tool_stdin_; }
  fbl::unique_fd& tool_stdout() { return tool_stdout_; }
  fbl::unique_fd& tool_stderr() { return tool_stderr_; }

  // This spawns a worker thread to feed the contents into the tool's stdin.
  // It resets tool_stdin() and moves ownership of the pipe end to the worker.
  void SendStdin(std::string contents);

  // These spawn worker threads that take ownership of the tool_stdin() or
  // tool_stdout() pipe and collect everything written into memory until Finish
  // returns.  Then collected_stdout() and collected_stderr() return them.
  void CollectStdout();
  void CollectStderr();

  std::string collected_stdout();
  std::string collected_stderr();

  const std::string& tmp_path() const { return tmp_path_; }

#ifdef __Fuchsia__
  void set_job(zx::unowned_job job) { job_ = job; }

  void set_resource(zx::unowned_resource resource) { resource_ = std::move(resource); }
#endif

 private:
  class SandboxLoop;

  std::string FilePathForRunner(const std::string& name) const;

  void SandboxCommand(PipedCommand& command);

  std::string tmp_path_;
  bool clear_tmp_ = false;
  std::list<File> files_;
  std::string collected_stdout_, collected_stderr_;
  std::thread stdin_thread_, stdout_thread_, stderr_thread_;
  fbl::unique_fd tool_stdin_, tool_stdout_, tool_stderr_;
#ifdef __Fuchsia__
  zx::process process_;
  zx::unowned_job job_ = zx::job::default_job();
  zx::unowned_resource resource_;
  std::unique_ptr<SandboxLoop> sandbox_loop_;
#else
  int process_ = -1;
#endif
};

std::string ToolPath(std::string tool);

}  // namespace testing
}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_
