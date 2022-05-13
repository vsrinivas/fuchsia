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

int reap_children() {
  for (;;) {
    int wstatus;
    if (waitpid(-1, &wstatus, 0) == -1) {
      if (errno == ECHILD) {
        // No more child, reaping is done.
        return 0;
      }
      // Another error is unexpected.
      perror("reap_children");
      return -1;
    }
    if (!WIFEXITED(wstatus)) {
      fprintf(stderr, "Child process did not exit normally.\n");
      return -1;
    }
    if (WEXITSTATUS(wstatus) != 0) {
      fprintf(stderr, "Child process did exit with an error: %d.\n", WEXITSTATUS(wstatus));
      return -1;
    }
  }
}

int open_main_terminal() {
  int fd = SAFE_SYSCALL(posix_openpt(O_RDWR));
  SAFE_SYSCALL(grantpt(fd));
  SAFE_SYSCALL(unlockpt(fd));
  return fd;
}

TEST(JobControl, BackgroundProcessGroupDoNotUpdateOnDeath) {
  // Reap children.
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  struct sigaction action;
  action.sa_handler = SIG_IGN;
  SAFE_SYSCALL(sigaction(SIGTTOU, &action, nullptr));

  if (SAFE_SYSCALL(fork()) == 0) {
    SAFE_SYSCALL(setsid());
    int main_terminal = open_main_terminal();
    int replica_terminal = SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR));

    if (SAFE_SYSCALL(tcgetpgrp(replica_terminal)) != getpid()) {
      fprintf(stderr, "Expected foreground process group (%d) to be the current one (%d).\n",
              tcgetpgrp(replica_terminal), getpid());
      exit(-1);
    }
    pid_t child_pid;
    if ((child_pid = SAFE_SYSCALL(fork())) == 0) {
      SAFE_SYSCALL(setpgid(0, 0));
      SAFE_SYSCALL(tcsetpgrp(replica_terminal, getpid()));

      if (SAFE_SYSCALL(tcgetpgrp(replica_terminal)) != getpid()) {
        fprintf(stderr, "Expected foreground process group (%d) to be the current one (%d).\n",
                tcgetpgrp(replica_terminal), getpid());
        exit(-1);
      }
    } else {
      SAFE_SYSCALL(setpgid(child_pid, child_pid));
      if (reap_children() != 0) {
        exit(-1);
      }

      if (SAFE_SYSCALL(tcgetpgrp(replica_terminal)) != child_pid) {
        fprintf(stderr, "Expected foreground process group (%d) to be the current one (%d).\n",
                tcgetpgrp(replica_terminal), child_pid);
        exit(-1);
      }

      if (setpgid(0, child_pid) != -1) {
        fprintf(stderr,
                "Expected not being able to join a process group that has no member anymore\n");
        exit(-1);
      }
      if (errno != EPERM) {
        fprintf(stderr, "Unexpected errnor. Expected EPERM (%d), got %d\n", EPERM, errno);
        exit(-1);
      }
    }

    // Ensure all forked process will exit and not reach back to gtest.
    exit(0);
  } else {
    // Wait for all children to die.
    ASSERT_EQ(0, reap_children());
  }
}

TEST(JobControl, OrphanedProcessGroupsReceivesSignal) {
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
          exit(-1);
        }
      } else {
        // Wait for the child to have stopped.
        SAFE_SYSCALL(waitid(P_PID, pid, nullptr, WSTOPPED));
      }
    } else {
      // Wait for the child to die and check it exited normally.
      exit(reap_children());
    }

    // Ensure all forked process will exit and not reach back to gtest.
    exit(0);
  } else {
    // Wait for all children to die.
    ASSERT_EQ(0, reap_children());
  }
}

}  // namespace
