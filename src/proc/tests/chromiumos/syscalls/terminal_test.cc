// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <termios.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {

int g_received_signal[64] = {};

void RecordSignalHandler(int signo) { g_received_signal[signo]++; }

void IgnoreSignal(int signal) {
  struct sigaction action;
  action.sa_handler = SIG_IGN;
  SAFE_SYSCALL(sigaction(signal, &action, nullptr));
}

void RecordSignal(int signal) {
  g_received_signal[signal] = 0;
  struct sigaction action;
  action.sa_handler = RecordSignalHandler;
  SAFE_SYSCALL(sigaction(signal, &action, nullptr));
}

long SleepNs(uint64_t count) {
  const uint64_t NS_PER_SECONDS = 1000000000;
  struct timespec ts = {.tv_sec = static_cast<time_t>(count / NS_PER_SECONDS),
                        .tv_nsec = static_cast<long>(count % NS_PER_SECONDS)};
  // TODO(qsr): Use nanosleep when starnix implements clock_nanosleep
  return syscall(SYS_nanosleep, &ts, nullptr);
}

int OpenMainTerminal(int additional_flags = 0) {
  int fd = SAFE_SYSCALL(posix_openpt(O_RDWR | additional_flags));
  SAFE_SYSCALL(grantpt(fd));
  SAFE_SYSCALL(unlockpt(fd));
  return fd;
}

TEST(JobControl, BackgroundProcessGroupDoNotUpdateOnDeath) {
  ForkHelper helper;

  IgnoreSignal(SIGTTOU);

  helper.RunInForkedProcess([&] {
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal();
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
        RecordSignal(SIGHUP);
        SAFE_SYSCALL(kill(getpid(), SIGTSTP));
        // At this point, a SIGHUP should have been received.
        // TODO(qsr): Remove the syscall that is there only because starnix
        // currently doesn't handle signal outside of syscalls, and doesn't
        // handle multiple signals at once.
        SAFE_SYSCALL(getpid());
        EXPECT_EQ(g_received_signal[SIGHUP], 1);
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
    int main_terminal = OpenMainTerminal();
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

    // Register a signal handler for sigusr1.
    RecordSignal(SIGUSR1);
    IgnoreSignal(SIGTTOU);
    IgnoreSignal(SIGHUP);

    // fork a child, move it to its own process group and makes it the
    // foreground one.
    helper.RunInForkedProcess([&] {
      SAFE_SYSCALL(setpgid(0, 0));
      SAFE_SYSCALL(tcsetpgrp(main_terminal, getpid()));

      // Register a signal handler for sigwinch.
      IgnoreSignal(SIGUSR1);
      RecordSignal(SIGWINCH);

      // Send a SIGUSR1 to notify our parent.
      SAFE_SYSCALL(kill(getppid(), SIGUSR1));

      // Wait for a SIGWINCH
      while (g_received_signal[SIGWINCH] == 0) {
        SleepNs(10e7);
      }
    });
    // Wait for SIGUSR1
    while (g_received_signal[SIGUSR1] == 0) {
      SleepNs(10e7);
    }

    // Resize the window, which must generate a SIGWINCH for the children.
    struct winsize ws = {.ws_row = 10, .ws_col = 10};
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSWINSZ, &ws));
  });
}

ssize_t FullRead(int fd, char* buf, size_t count) {
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

    int main_terminal = OpenMainTerminal(O_NONBLOCK);
    SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

    SAFE_SYSCALL(open("/dev/tty", O_RDWR));
    int other_terminal = SAFE_SYSCALL(open("/dev/tty", O_RDWR));
    struct stat stats;
    SAFE_SYSCALL(fstat(other_terminal, &stats));

    ASSERT_EQ(major(stats.st_rdev), 5u);
    ASSERT_EQ(minor(stats.st_rdev), 0u);

    ASSERT_EQ(write(other_terminal, "h\n", 2), 2);
    char buf[20];
    ASSERT_EQ(FullRead(main_terminal, buf, 20), 3);
    ASSERT_EQ(strncmp(buf, "h\r\n", 3), 0);
  });
}

TEST(Pty, ioctl_TCSETSF) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal();

    struct termios config;
    SAFE_SYSCALL(ioctl(main_terminal, TCGETS, &config));
    SAFE_SYSCALL(ioctl(main_terminal, TCSETSF, &config));
  });
}

void FullWrite(int fd, char* buffer, ssize_t size) { ASSERT_EQ(write(fd, buffer, size), size); }

TEST(Pty, EndOfFile) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    // Create a new session here.
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal();
    int replica_terminal = SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR | O_NONBLOCK));

    char source_buffer[2];
    source_buffer[0] = 4;  // ^D
    source_buffer[1] = '\n';
    char target_buffer[2];

    FullWrite(main_terminal, source_buffer, 1);
    ASSERT_EQ(0, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(-1, read(replica_terminal, target_buffer, 2));
    ASSERT_EQ(EAGAIN, errno);

    FullWrite(main_terminal, source_buffer, 2);
    ASSERT_EQ(0, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(1, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ('\n', target_buffer[0]);

    FullWrite(main_terminal, source_buffer, 1);
    FullWrite(main_terminal, source_buffer + 1, 1);
    ASSERT_EQ(0, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(1, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ('\n', target_buffer[0]);

    source_buffer[0] = 4;  // ^D
    source_buffer[1] = 4;  // ^D
    FullWrite(main_terminal, source_buffer, 2);
    ASSERT_EQ(0, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(0, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(-1, read(replica_terminal, target_buffer, 2));
    ASSERT_EQ(EAGAIN, errno);

    source_buffer[0] = ' ';
    source_buffer[1] = 4;  // ^D
    FullWrite(main_terminal, source_buffer, 2);
    ASSERT_EQ(1, SAFE_SYSCALL(read(replica_terminal, target_buffer, 2)));
    ASSERT_EQ(' ', target_buffer[0]);
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
      int main_terminal = OpenMainTerminal();
      SAFE_SYSCALL(ioctl(main_terminal, TIOCSCTTY, 0));

      // Register a signal handler for sigusr1.
      RecordSignal(SIGUSR1);
      IgnoreSignal(SIGTTOU);
      IgnoreSignal(SIGHUP);

      // fork a child, move it to its own process group and makes it the
      // foreground one.
      pid_t child_pid = helper.RunInForkedProcess([&] {
        SAFE_SYSCALL(setpgid(0, 0));
        SAFE_SYSCALL(tcsetpgrp(main_terminal, getpid()));

        // Send a SIGUSR1 to notify our parent.
        SAFE_SYSCALL(kill(getppid(), SIGUSR1));

        // Wait to be killed by our parent.
        for (;;) {
          SleepNs(10e8);
        }
      });
      // Wait for SIGUSR1
      while (g_received_signal[SIGUSR1] == 0) {
        SleepNs(10e7);
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

TEST(Pty, CloseMainTerminal) {
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    IgnoreSignal(SIGHUP);
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal(O_NONBLOCK | O_NOCTTY);
    int replica_terminal =
        SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR | O_NONBLOCK | O_NOCTTY));
    ASSERT_EQ(open("/dev/tty", O_RDWR), -1);
    ASSERT_EQ(errno, ENXIO);
    close(main_terminal);
    char buffer[1];
    ASSERT_EQ(read(replica_terminal, buffer, 1), 0);
    ASSERT_EQ(write(replica_terminal, buffer, 1), -1);
    EXPECT_EQ(EIO, errno);

    short all_events = POLLIN | POLLPRI | POLLOUT | POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
    struct pollfd fds = {replica_terminal, all_events, 0};
    ASSERT_EQ(1, SAFE_SYSCALL(poll(&fds, 1, -1)));
    EXPECT_EQ(fds.revents, POLLIN | POLLOUT | POLLERR | POLLHUP);
  });
}

TEST(Pty, CloseReplicaTerminal) {
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal(O_NONBLOCK | O_NOCTTY);
    int replica_terminal =
        SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR | O_NONBLOCK | O_NOCTTY));
    ASSERT_EQ(open("/dev/tty", O_RDWR), -1);
    ASSERT_EQ(errno, ENXIO);
    close(replica_terminal);

    char buffer[1];
    ASSERT_EQ(read(main_terminal, buffer, 1), -1);
    EXPECT_EQ(EIO, errno);

    short all_events = POLLIN | POLLPRI | POLLOUT | POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
    struct pollfd fds = {main_terminal, all_events, 0};
    ASSERT_EQ(1, SAFE_SYSCALL(poll(&fds, 1, -1)));
    ASSERT_EQ(fds.revents, POLLOUT | POLLHUP);

    ASSERT_EQ(write(main_terminal, buffer, 1), 1);
  });
}

TEST(Pty, DetectReplicaClosing) {
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    // Create a new session here, and associate it with the new terminal.
    SAFE_SYSCALL(setsid());
    int main_terminal = OpenMainTerminal(O_NOCTTY);
    int replica_terminal = SAFE_SYSCALL(open(ptsname(main_terminal), O_RDWR | O_NOCTTY));

    struct pollfd fds = {main_terminal, POLLIN, 0};

    RecordSignal(SIGUSR1);
    pid_t child_pid = helper.RunInForkedProcess([&] {
      close(main_terminal);
      RecordSignal(SIGUSR2);
      SAFE_SYSCALL(kill(getppid(), SIGUSR1));
      // Wait for SIGUSR2
      while (g_received_signal[SIGUSR2] == 0) {
        SleepNs(10e7);
      }
    });

    close(replica_terminal);
    // Wait for SIGUSR1
    while (g_received_signal[SIGUSR1] == 0) {
      SleepNs(10e7);
    }
    SAFE_SYSCALL(kill(child_pid, SIGUSR2));
    ASSERT_EQ(1, SAFE_SYSCALL(poll(&fds, 1, 10000)));
    ASSERT_EQ(fds.revents, POLLHUP);
  });
}

}  // namespace
