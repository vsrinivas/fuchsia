// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>

#include <map>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>
#include <linenoise/linenoise.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kRunChildFlag[] = "--run-child-main";

void AddPipe(fdio_spawn_action_t* action, int target_fd, int* fd_out) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_pipe_half(fd_out, &handle);
  ASSERT_OK(status);
  action->action = FDIO_SPAWN_ACTION_ADD_HANDLE;
  action->h.id = PA_HND(PA_FD, target_fd);
  action->h.handle = handle;
}

struct MultiprocessInfo {
  fbl::unique_fd stdin_write;
  fbl::unique_fd stdout_read;
  zx::process child;
};

std::unique_ptr<MultiprocessInfo> RunChild(const char* child_main) {
  std::unique_ptr<MultiprocessInfo> ret(new MultiprocessInfo);

  constexpr size_t kActionCount = 3;
  fdio_spawn_action_t actions[kActionCount];

  int stdin_parent_side = -1;
  AddPipe(&actions[0], STDIN_FILENO, &stdin_parent_side);
  ret->stdin_write.reset(stdin_parent_side);

  int stdout_parent_side = -1;
  AddPipe(&actions[1], STDOUT_FILENO, &stdout_parent_side);
  ret->stdout_read.reset(stdout_parent_side);

  actions[2].action = FDIO_SPAWN_ACTION_CLONE_FD;
  actions[2].fd.local_fd = STDERR_FILENO;
  actions[2].fd.target_fd = STDERR_FILENO;

  // Pass the filesystem namespace, parent environment, and default job to the child, but don't
  // include any other file handles, preferring to set them up explicitly.
  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  std::string command = std::string(root_dir) + "/test/sys/linenoise-test-test";
  std::vector<const char*> argv;
  argv.push_back(command.c_str());
  argv.push_back(kRunChildFlag);
  argv.push_back(child_main);
  argv.push_back(nullptr);

  char error_message[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process child;
  zx_status_t status =
      fdio_spawn_etc(ZX_HANDLE_INVALID, flags, command.c_str(), argv.data(), nullptr, kActionCount,
                     actions, child.reset_and_get_address(), error_message);
  ZX_ASSERT(status == ZX_OK);
  ret->child = std::move(child);

  return ret;
}

int64_t JoinChild(std::unique_ptr<MultiprocessInfo> info) {
  zx_status_t status = info->child.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
  ZX_ASSERT(status == ZX_OK);
  zx_info_process_t proc_info{};
  status = info->child.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return proc_info.return_code;
}

std::map<std::string, int(*)()>& GetChildMainFunctionMap() {
  static auto* map = new std::map<std::string, int(*)()>();
  return *map;
}

class AppendChildMain {
 public:
  AppendChildMain(const std::string& test_name, int (*main_function_pointer)()) {
    ZX_ASSERT(GetChildMainFunctionMap().find(test_name) == GetChildMainFunctionMap().end());
    GetChildMainFunctionMap()[test_name] = main_function_pointer;
  }
};

#define CHILD_MAIN(test_main)                                             \
  int test_main();                                                        \
  namespace {                                                             \
  AppendChildMain AppendChildMain##_##test_main(#test_main, (test_main)); \
  } /* namespace */                                                       \
  int test_main()

std::string ReadString(MultiprocessInfo* child) {
  char buf[4096];
  ssize_t bytes_read = read(child->stdout_read.get(), buf, sizeof(buf));
  ZX_ASSERT(bytes_read > 0);
  ZX_ASSERT(bytes_read < static_cast<ssize_t>(countof(buf) - 1));
  buf[bytes_read] = 0;
  return std::string(buf);
}

// Emulate typing something in.
void Send(MultiprocessInfo* child, const std::string& input) {
  for (char c : input) {
    write(child->stdin_write.get(), &c, 1);
    usleep(100);
  }
}

CHILD_MAIN(TestWritingToStdout) {
  printf("hello");
  return 0;
}

TEST(Linenoise, TestMultiprocessHelper) {
  auto child = RunChild("TestWritingToStdout");
  ASSERT_EQ(ReadString(child.get()), "hello");
  ASSERT_EQ(JoinChild(std::move(child)), 0);
}

CHILD_MAIN(RunLinenoiseWithLongPrompt) {
  linenoiseHistorySetMaxLen(10);
  std::string long_str(1000, 'X');
  char* line = linenoise(long_str.c_str());
  linenoiseFree(line);
  return 0;
}

// Test for reproduction in fxbug.dev/33554 where a long prompt caused a crash.
TEST(Linenoise, CrashLongPrompt) {
  auto child = RunChild("RunLinenoiseWithLongPrompt");

  // linenoise requests information terminal information here, we have to stub out responses (always
  // returning a cursor position of 10, 100 when it asks).

  // Handle initial request for console position.
  char buf[32];
  ASSERT_EQ(read(child->stdout_read.get(), buf, 4), 4);  // \x1b[6n
  sprintf(buf, "\x1b[%d;%dR", 10, 100);
  write(child->stdin_write.get(), buf, strlen(buf));

  // Handle moving followed by request.
  ASSERT_EQ(read(child->stdout_read.get(), buf, 6), 6);  // \x1b[999C
  ASSERT_EQ(read(child->stdout_read.get(), buf, 4), 4);  // \x1b[6n
  sprintf(buf, "\x1b[%d;%dR", 10, 100);
  write(child->stdin_write.get(), buf, strlen(buf));

  // Send some input with the long prompt (set by the child process).
  Send(child.get(), "l\n");

  // And ensure that the process didn't crash (for example, with an exit code of
  // ZX_TASK_RETCODE_EXCEPTION_KILL).
  ASSERT_EQ(JoinChild(std::move(child)), 0);
}

}  // namespace

extern "C" {
// Encourage linenoise to go through the same path as normal at-a-console interactions.
int isatty(int fd) { return 1; }
}

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], kRunChildFlag) == 0) {
    // Thunk back into a pseudo-main for a child process if a special flag is specified.
    ZX_ASSERT(GetChildMainFunctionMap().find(argv[2]) != GetChildMainFunctionMap().end());
    return GetChildMainFunctionMap().find(argv[2])->second();
  }
  return RUN_ALL_TESTS(argc, argv);
}
