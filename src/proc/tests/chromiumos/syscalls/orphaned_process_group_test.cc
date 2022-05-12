// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/prctl.h>

#include <gtest/gtest.h>

namespace {

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define SAFE_SYSCALL(X)                    \
  ({                                       \
    int retval;                            \
    retval = (X);                          \
    if (retval < 0) {                      \
      perror(STRINGIFY(__LINE__) ": " #X); \
      exit(retval);                        \
    };                                     \
    retval;                                \
  })

int received_signal = -1;

void sig_hup(int signo) { received_signal = signo; }

bool reap_children() {
  for (;;) {
    int wstatus;
    if (waitpid(-1, &wstatus, 0) == -1) {
      if (errno == ECHILD) {
        // No more child, reaping is done.
        return true;
      }
      // Another error is unexpected.
      perror("reap_children");
      return false;
    }
    if (!WIFEXITED(wstatus)) {
      fprintf(stderr, "Child process did not exit normally.\n");
      return false;
    }
    if (WEXITSTATUS(wstatus) != 0) {
      fprintf(stderr, "Child process did exit with an error: %d.\n", WEXITSTATUS(wstatus));
      return false;
    }
  }
}

TEST(OrphanedProcessGroups, OrphanedProcessGroupsReceivesSignal) {
  // Reap children.
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  if (SAFE_SYSCALL(fork()) == 0) {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());

    if (SAFE_SYSCALL(fork()) == 0) {
      // Create a new, non leader, process group.
      SAFE_SYSCALL(setpgid(0, 0));
      pid_t pid;
      if ((pid = SAFE_SYSCALL(fork())) == 0) {
        // Deepest child. Set a SIGHUP handler, stop ourself, and check that we
        // are restarted and received the expected SIGHUP when our immediate
        // parent dies
        struct sigaction action;
        action.sa_handler = sig_hup;
        SAFE_SYSCALL(sigaction(SIGHUP, &action, nullptr));
        SAFE_SYSCALL(kill(getpid(), SIGTSTP));
        // At this point, a SIGHUP should have been received.
        // TODO(qsr): Remove the syscall that is there only because starnix
        // currently doesn't handle signal outside of syscalls, and doesn't
        // handle multiple signals at once.
        SAFE_SYSCALL(getpid());
        if (received_signal != SIGHUP) {
          fprintf(stderr, "Did not received expected SIGHUP\n");
          exit(1);
        }
      } else {
        // Wait for the child to have stopped.
        SAFE_SYSCALL(waitid(P_PID, pid, nullptr, WSTOPPED));
      }
    } else {
      // Wait for the child to die and check it exited normally.
      if (!reap_children()) {
        exit(1);
      }
    }

    // Ensure all forked process will exit and not reach back to gtest.
    exit(0);
  } else {
    // Wait for all children to die.
    ASSERT_TRUE(reap_children());
  }
}

}  // namespace
