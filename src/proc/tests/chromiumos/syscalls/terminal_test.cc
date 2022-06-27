// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <termios.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {

int received_signal[64] = {};

void record_signal(int signo) { received_signal[signo]++; }

void ignore_signal(int signal) {
  struct sigaction action;
  action.sa_handler = SIG_IGN;
  SAFE_SYSCALL(sigaction(signal, &action, nullptr));
}

void register_signal_handler(int signal) {
  received_signal[signal] = 0;
  struct sigaction action;
  action.sa_handler = record_signal;
  SAFE_SYSCALL(sigaction(signal, &action, nullptr));
}

long sleep_ns(int count) {
  struct timespec ts = {.tv_sec = 0, .tv_nsec = count};
  // TODO(qsr): Use nanosleep when starnix implements clock_nanosleep
  return syscall(SYS_nanosleep, &ts, nullptr);
}

int open_main_terminal() {
  int fd = SAFE_SYSCALL(posix_openpt(O_RDWR | O_NONBLOCK));
  SAFE_SYSCALL(grantpt(fd));
  SAFE_SYSCALL(unlockpt(fd));
  return fd;
}

TEST(JobControl, BackgroundProcessGroupDoNotUpdateOnDeath) {
  ForkHelper helper;

  ignore_signal(SIGTTOU);

  helper.RunInForkedProcess([&] {
    SAFE_SYSCALL(setsid());
    int main_terminal = open_main_terminal();
    int replica_terminal = SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR));

    ASSERT_EQ(SAFE_SYSCALL(tcgetpgrp(replica_terminal)), getpid());
    pid_t child_pid = helper.RunInForkedProcess([&] {
      SAFE_SYSCALL(setpgid(0, 0));
      SAFE_SYSCALL(tcsetpgrp(replica_terminal, getpid()));

      ASSERT_EQ(SAFE_SYSCALL(tcgetpgrp(replica_terminal)), getpid());
    });

    // Wait for the child to die.
    ASSERT_TRUE(helper.WaitForChildren());

    // The foreground process group should still be the one from the child.
    ASSERT_EQ(SAFE_SYSCALL(tcgetpgrp(replica_terminal)), child_pid);

    ASSERT_EQ(setpgid(0, child_pid), -1)
        << "Expected not being able to join a process group that has no member anymore";
    ASSERT_EQ(errno, EPERM);
  });
}

TEST(JobControl, OrphanedProcessGroupsReceivesSignal) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());

    helper.RunInForkedProcess([&] {
      // Create a new, non leader, process group.
      SAFE_SYSCALL(setpgid(0, 0));
      pid_t pid = helper.RunInForkedProcess([&] {
        // Deepest child. Set a SIGHUP handler, stop ourself, and check that we
        // are restarted and received the expected SIGHUP when our immediate
        // parent dies
        register_signal_handler(SIGHUP);
        SAFE_SYSCALL(kill(getpid(), SIGTSTP));
        // At this point, a SIGHUP should have been received.
        // TODO(qsr): Remove the syscall that is there only because starnix
        // currently doesn't handle signal outside of syscalls, and doesn't
        // handle multiple signals at once.
        SAFE_SYSCALL(getpid());
        EXPECT_EQ(received_signal[SIGHUP], 1);
      });
      // Wait for the child to have stopped.
      SAFE_SYSCALL(waitid(P_PID, pid, nullptr, WSTOPPED));
    });
    // Wait for the child to die.
    ASSERT_TRUE(helper.WaitForChildren());
  });
}

TEST(Pty, SigWinch) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = open_main_terminal();
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

    // Register a signal handler for sigusr1.
    register_signal_handler(SIGUSR1);
    ignore_signal(SIGTTOU);
    ignore_signal(SIGHUP);

    // fork a child, move it to its own process group and makes it the
    // foreground one.
    helper.RunInForkedProcess([&] {
      SAFE_SYSCALL(setpgid(0, 0));
      SAFE_SYSCALL(tcsetpgrp(main_terminal, getpid()));

      // Register a signal handler for sigwinch.
      ignore_signal(SIGUSR1);
      register_signal_handler(SIGWINCH);

      // Send a SIGUSR1 to notify our parent.
      SAFE_SYSCALL(kill(getppid(), SIGUSR1));

      // Wait for a SIGWINCH
      while (received_signal[SIGWINCH] == 0) {
        sleep_ns(10e7);
      }
    });
    // Wait for SIGUSR1
    while (received_signal[SIGUSR1] == 0) {
      sleep_ns(10e7);
    }

    // Resize the window, which must generate a SIGWINCH for the children.
    struct winsize ws = {.ws_row = 10, .ws_col = 10};
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSWINSZ, &ws));
  });
}

ssize_t full_read(int fd, char *buf, size_t count) {
  ssize_t result = 0;
  while (count > 0) {
    ssize_t read_result = read(fd, buf, count);
    if (read_result == -1) {
      if (errno == EAGAIN) {
        break;
      }
      return -1;
    }
    buf += read_result;
    count -= read_result;
    result += read_result;
  }
  return result;
}

TEST(Pty, OpenDevTTY) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());

    int main_terminal = open_main_terminal();
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

    SAFE_SYSCALL(open("/dev/tty", O_RDWR));
    int other_terminal = SAFE_SYSCALL(open("/dev/tty", O_RDWR));
    struct stat stats;
    SAFE_SYSCALL(fstat(other_terminal, &stats));

    ASSERT_EQ(major(stats.st_rdev), 5u);
    ASSERT_EQ(minor(stats.st_rdev), 0u);

    ASSERT_EQ(write(other_terminal, "h\n", 2), 2);
    char buf[20];
    ASSERT_EQ(full_read(main_terminal, buf, 20), 3);
    ASSERT_EQ(strncmp(buf, "h\r\n", 3), 0);
  });
}

TEST(Pty, ioctl_TCSETSF) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = open_main_terminal();

    struct termios config;
    SAFE_SYSCALL(ioctl(main_terminal, TCGETS, &config));
    SAFE_SYSCALL(ioctl(main_terminal, TCSETSF, &config));
  });
}

TEST(Pty, SendSignals) {
  ForkHelper helper;

  std::map<int, char> signal_and_control_character;
  signal_and_control_character[SIGINT] = 3;
  signal_and_control_character[SIGQUIT] = 28;
  signal_and_control_character[SIGSTOP] = 26;

  for (auto [s, c] : signal_and_control_character) {
    auto signal = s;
    auto character = c;

    helper.RunInForkedProcess([&] {
      // Create a new session here, and associate it with the new terminal.
      SAFE_SYSCALL(setsid());
      int main_terminal = open_main_terminal();
      SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

      // Register a signal handler for sigusr1.
      register_signal_handler(SIGUSR1);
      ignore_signal(SIGTTOU);
      ignore_signal(SIGHUP);

      // fork a child, move it to its own process group and makes it the
      // foreground one.
      pid_t child_pid = helper.RunInForkedProcess([&] {
        SAFE_SYSCALL(setpgid(0, 0));
        SAFE_SYSCALL(tcsetpgrp(main_terminal, getpid()));

        // Send a SIGUSR1 to notify our parent.
        SAFE_SYSCALL(kill(getppid(), SIGUSR1));

        // Wait to be killed by our parent.
        for (;;) {
          sleep_ns(10e8);
        }
      });
      // Wait for SIGUSR1
      while (received_signal[SIGUSR1] == 0) {
        sleep_ns(10e7);
      }

      // Send control character.
      char buffer[1];
      buffer[0] = character;
      SAFE_SYSCALL(write(main_terminal, buffer, 1));

      int wstatus;
      pid_t received_pid = SAFE_SYSCALL(waitpid(child_pid, &wstatus, WUNTRACED));
      ASSERT_EQ(received_pid, child_pid);
      if (signal == SIGSTOP) {
        ASSERT_TRUE(WIFSTOPPED(wstatus));
        // Ensure the children is called, even when only stopped.
        SAFE_SYSCALL(kill(child_pid, SIGKILL));
        SAFE_SYSCALL(waitpid(child_pid, NULL, 0));
      } else {
        ASSERT_TRUE(WIFSIGNALED(wstatus));
        ASSERT_EQ(WTERMSIG(wstatus), signal);
      }
    });
    ASSERT_TRUE(helper.WaitForChildren());
  }
}

}  // namespace
