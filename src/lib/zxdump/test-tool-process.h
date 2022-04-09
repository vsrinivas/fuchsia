// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_
#define SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_

#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef __Fuchsia__
#include <lib/zx/process.h>
#endif

#include <fbl/unique_fd.h>

namespace zxdump::testing {

// This manages a command-line tool process to be run in a sandbox (on Fuchsia)
// or from the build directory (on other hosts) with specified input and output
// files and fully captured stdin/stdout/stderr.
class TestToolProcess {
 public:
  class File {
   public:
    File() = default;
    File(const File&) = delete;
    File(File&&) = default;

    ~File();

    // Return the name of the file as seen by the tool run by Start.
    // This is used in composing the arguments to pass to Start.
    std::string name() const;

    // Create the file so it can be written and used as input to the tool.
    // This is used before Start, with name() used to compose the arguments.
    fbl::unique_fd CreateInput();

    // Read the file after it's been written by the tool.
    // This is used  after Finish.
    fbl::unique_fd OpenOutput();

    // Uses OpenOutput to read the whole file.
    std::string OutputContents();

   private:
    friend TestToolProcess;
    friend std::string FilePathForRunner(const File&);
    friend std::string FilePathForTool(const File&);

    std::string name_;
  };

  TestToolProcess() = default;
  TestToolProcess(const TestToolProcess&) = delete;
  TestToolProcess(TestToolProcess&&) = default;

  ~TestToolProcess();

  // Return a file name that can be passed to the tool via its Start arguments.
  // The file name will include the given name string for debugging purposes,
  // but will be unique among all MakeFileName calls on this TestToolProcess.
  // Whether these are input files the test code writes for the tool to read,
  // or output files the tool writes and the test code checks afterwards, they
  // will be cleaned up when the TestToolProcess object is destroyed.
  File& MakeFile(std::string_view name);

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

 private:
  std::vector<File> files_;
  std::string collected_stdout_, collected_stderr_;
  std::thread stdin_thread_, stdout_thread_, stderr_thread_;
  fbl::unique_fd tool_stdin_, tool_stdout_, tool_stderr_;
#ifdef __Fuchsia__
  zx::process process_;
#else
  int process_ = -1;
#endif
};

std::string ToolPath(std::string tool);

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_TEST_TOOL_PROCESS_H_
