// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

// argv[0]
static char* program_path;

// We have to poll a thread's state as there is no way to wait for it to
// transition states. Wait this amount of time. Generally the thread won't
// take very long so this is a compromise between polling too frequently and
// waiting too long.
constexpr zx_duration_t THREAD_BLOCKED_WAIT_DURATION = ZX_MSEC(1);

static const char test_child_name[] = "test-child";

// The maximum number of handles we send with send_msg_with_handles.
constexpr uint32_t MAX_NUM_MSG_HANDLES = 2;

// The number of handles used in the wait-many test.
constexpr uint32_t NUM_WAIT_MANY_HANDLES = MAX_NUM_MSG_HANDLES;

const zx_port_packet_t port_test_packet = {
    .key = 42u,
    .type = ZX_PKT_TYPE_USER,
    .status = -42,
    .user =
        {
            .u64 = {1, 2, 3, 4},
        },
};

const zx_time_t interrupt_signaled_timestamp = 12345;

enum MessageType : uint32_t {
  MSG_DONE,
  MSG_PASS,
  MSG_FAIL,
  MSG_PROCEED,
  MSG_THREAD_HANDLE_REQUEST,
  MSG_THREAD_HANDLE_RESPONSE,
  MSG_SLEEP_TEST,
  MSG_FUTEX_TEST,
  MSG_PORT_TEST,
  MSG_CHANNEL_TEST,
  MSG_WAIT_ONE_TEST,
  MSG_WAIT_MANY_TEST,
  MSG_INTERRUPT_TEST,
};

struct Message {
  MessageType type;
  uint32_t num_handles;
  zx_handle_t handles[MAX_NUM_MSG_HANDLES];
};

static void send_msg_with_handles(zx_handle_t channel, MessageType type,
                                  zx_handle_t* optional_handles, uint32_t num_handles) {
  uint32_t data = type;
  printf("sending message %d on handle %u, with %u handles\n", type, channel, num_handles);
  zx_status_t status =
      zx_channel_write(channel, 0, &data, sizeof(data), optional_handles, num_handles);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

static void send_msg(zx_handle_t channel, MessageType type) {
  send_msg_with_handles(channel, type, nullptr, 0);
}

static bool recv_msg(zx_handle_t channel, Message* msg) {
  printf("waiting for message on handle %u\n", channel);

  if (!tu_channel_wait_readable(channel)) {
    printf("peer closed while trying to read message\n");
    return false;
  }

  uint32_t data;
  uint32_t num_bytes = sizeof(data);
  msg->num_handles = MAX_NUM_MSG_HANDLES;
  zx_status_t status = zx_channel_read(channel, 0, &data, &msg->handles[0], num_bytes,
                                       msg->num_handles, &num_bytes, &msg->num_handles);
  if (status != ZX_OK) {
    printf("ERROR: failed to read message: %s\n", zx_status_get_string(status));
    return false;
  }
  if (num_bytes != sizeof(data)) {
    printf("ERROR: unexpected message size, %u != %zu\n", num_bytes, sizeof(data));
    return false;
  }

  msg->type = static_cast<MessageType>(data);
  printf("received message %u\n", msg->type);
  return true;
}

static void recv_specific_msg(zx_handle_t channel, MessageType expected_type) {
  Message msg;
  ASSERT_TRUE(recv_msg(channel, &msg));
  ASSERT_EQ(msg.type, expected_type);
}

static void do_msg_thread_handle_request(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 0) {
    printf("ERROR: wrong number handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }
  auto self = zx_thread_self();
  send_msg_with_handles(channel, MSG_THREAD_HANDLE_RESPONSE, &self, 1);
}

static void do_msg_sleep_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 0) {
    printf("ERROR: wrong number handles\n");
    // There's no point in sending MSG_FAIL here as the test can never
    // receive MSG_PASS.
    return;
  }
  zx_nanosleep(ZX_TIME_INFINITE);
  /* NOTREACHED */
}

static void do_msg_futex_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 0) {
    printf("ERROR: wrong number handles\n");
    // There's no point in sending MSG_FAIL here as the test can never
    // receive MSG_PASS.
    return;
  }

  int futex_value = 0;
  zx_status_t __UNUSED status = zx_futex_wait(&futex_value, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
  /* NOTREACHED*/
}

static void do_msg_port_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 1) {
    printf("ERROR: wrong number handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  auto port = msg->handles[0];
  zx_port_packet_t packet;
  auto status = zx_port_wait(port, ZX_TIME_INFINITE, &packet);
  zx_handle_close(port);
  if (status != ZX_OK) {
    printf("ERROR: port_wait failed: %d/%s\n", status, zx_status_get_string(status));
    send_msg(channel, MSG_FAIL);
    return;
  }

  if (packet.key != port_test_packet.key || packet.type != port_test_packet.type ||
      packet.status != port_test_packet.status ||
      memcmp(&packet.user, &port_test_packet.user, sizeof(zx_port_packet_t::user)) != 0) {
    printf("ERROR: bad data in packet\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  send_msg(channel, MSG_PASS);
}

static void do_msg_channel_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 1) {
    printf("ERROR: wrong number handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  auto test_channel = msg->handles[0];
  uint32_t write_data = 0xdeadbeef;
  uint32_t read_data;
  zx_channel_call_args_t args = {
      .wr_bytes = &write_data,
      .wr_handles = nullptr,
      .rd_bytes = &read_data,
      .rd_handles = nullptr,
      .wr_num_bytes = sizeof(write_data),
      .wr_num_handles = 0,
      .rd_num_bytes = sizeof(read_data),
      .rd_num_handles = 0,
  };

  uint32_t actual_num_bytes = 0;
  uint32_t actual_num_handles = 0;
  auto status = zx_channel_call(test_channel, 0, ZX_TIME_INFINITE, &args, &actual_num_bytes,
                                &actual_num_handles);
  zx_handle_close(test_channel);
  if (status == ZX_ERR_PEER_CLOSED) {
    // ok
  } else {
    printf("ERROR: channel_call didn't get PEER_CLOSED: %d/%s\n", status,
           zx_status_get_string(status));
    send_msg(channel, MSG_FAIL);
    return;
  }

  send_msg(channel, MSG_PASS);
}

static void do_msg_wait_one_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 1) {
    printf("ERROR: wrong number handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  // The test waits for this to make sure it doesn't see us blocked waiting
  // for a Message. This is sent for wait_one and wait_many so that we don't
  // have to know which one is used to wait for messages.
  send_msg(channel, MSG_PROCEED);

  zx_signals_t observed = 0u;
  auto status =
      zx_object_wait_one(msg->handles[0], ZX_EVENTPAIR_PEER_CLOSED, ZX_TIME_INFINITE, &observed);
  zx_handle_close(msg->handles[0]);
  if (status != ZX_OK) {
    printf("ERROR: wait_one failed: %d/%s\n", status, zx_status_get_string(status));
    send_msg(channel, MSG_FAIL);
    return;
  }

  if (!(observed & ZX_EVENTPAIR_PEER_CLOSED)) {
    printf("ERROR: ZX_EVENTPAIR_PEER_CLOSED not observed\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  send_msg(channel, MSG_PASS);
}

static void do_msg_wait_many_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles > NUM_WAIT_MANY_HANDLES) {
    printf("ERROR: too many handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  // The test waits for this to make sure it doesn't see us blocked waiting
  // for a Message. This is sent for wait_one and wait_many so that we don't
  // have to know which one is used to wait for messages.
  send_msg(channel, MSG_PROCEED);

  uint32_t num_handles = msg->num_handles;
  zx_wait_item_t items[num_handles];
  for (uint32_t i = 0; i < num_handles; ++i) {
    items[i].handle = msg->handles[i];
    items[i].waitfor = ZX_EVENTPAIR_PEER_CLOSED;
  }
  auto status = zx_object_wait_many(&items[0], num_handles, ZX_TIME_INFINITE);
  for (uint32_t i = 0; i < num_handles; ++i) {
    zx_handle_close(msg->handles[i]);
  }
  if (status != ZX_OK) {
    printf("ERROR: wait_many failed: %d/%s\n", status, zx_status_get_string(status));
    send_msg(channel, MSG_FAIL);
    return;
  }

  // At least one of the handles should have gotten PEER_CLOSED.
  bool got_peer_closed = false;
  for (uint32_t i = 0; i < num_handles; ++i) {
    if (items[i].pending & ZX_EVENTPAIR_PEER_CLOSED) {
      got_peer_closed = true;
      break;
    }
  }
  if (!got_peer_closed) {
    printf("ERROR: ZX_EVENTPAIR_PEER_CLOSED not observed\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  send_msg(channel, MSG_PASS);
}

static void do_msg_interrupt_test(zx_handle_t channel, const Message* msg) {
  if (msg->num_handles != 1) {
    printf("ERROR: wrong number handles\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  auto interrupt = msg->handles[0];
  zx_time_t timestamp;
  auto status = zx_interrupt_wait(interrupt, &timestamp);
  zx_handle_close(interrupt);
  if (status != ZX_OK) {
    printf("ERROR: interrupt_wait failed: %d/%s\n", status, zx_status_get_string(status));
    send_msg(channel, MSG_FAIL);
    return;
  }

  if (timestamp != interrupt_signaled_timestamp) {
    printf("ERROR: interrupt timestamp mismatch\n");
    send_msg(channel, MSG_FAIL);
    return;
  }

  send_msg(channel, MSG_PASS);
}

static void msg_loop(zx_handle_t channel) {
  bool my_done_tests = false;

  while (!my_done_tests) {
    Message msg;
    msg.num_handles = static_cast<uint32_t>(std::size(msg.handles));
    if (!recv_msg(channel, &msg)) {
      printf("ERROR: while receiving msg\n");
      return;
    }

    switch (msg.type) {
      case MSG_DONE:
        my_done_tests = true;
        break;
      case MSG_THREAD_HANDLE_REQUEST:
        do_msg_thread_handle_request(channel, &msg);
        break;
      case MSG_SLEEP_TEST:
        do_msg_sleep_test(channel, &msg);
        break;
      case MSG_FUTEX_TEST:
        do_msg_futex_test(channel, &msg);
        break;
      case MSG_PORT_TEST:
        do_msg_port_test(channel, &msg);
        break;
      case MSG_CHANNEL_TEST:
        do_msg_channel_test(channel, &msg);
        break;
      case MSG_WAIT_ONE_TEST:
        do_msg_wait_one_test(channel, &msg);
        break;
      case MSG_WAIT_MANY_TEST:
        do_msg_wait_many_test(channel, &msg);
        break;
      case MSG_INTERRUPT_TEST:
        do_msg_interrupt_test(channel, &msg);
        break;
      default:
        printf("ERROR: unknown message received: %u\n", msg.type);
        break;
    }
  }
}

static void __NO_RETURN test_child(void) {
  printf("Test child starting.\n");
  zx_handle_t channel = zx_take_startup_handle(PA_USER0);
  if (channel == ZX_HANDLE_INVALID)
    tu_fatal("zx_take_startup_handle", ZX_ERR_BAD_HANDLE - 1000);
  msg_loop(channel);
  printf("Test child exiting.\n");
  exit(0);
}

static springboard_t* setup_test_child(zx_handle_t job, const char* arg, zx_handle_t* out_channel) {
  printf("Starting test child %s.\n", arg);
  zx_handle_t our_channel, their_channel;
  zx_status_t status = zx_channel_create(0, &our_channel, &their_channel);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  const char* test_child_path = program_path;
  const char* const argv[] = {
      test_child_path,
      arg,
  };
  int argc = std::size(argv);
  zx_handle_t handles[1] = {their_channel};
  uint32_t handle_ids[1] = {PA_USER0};
  *out_channel = our_channel;
  springboard_t* sb =
      tu_launch_init(job, test_child_name, argc, argv, 0, NULL, 1, handles, handle_ids);
  printf("Test child setup.\n");
  return sb;
}

static void start_test_child(zx_handle_t job, const char* arg, zx_handle_t* out_child,
                             zx_handle_t* out_channel) {
  springboard_t* sb = setup_test_child(job, arg, out_channel);
  *out_child = tu_launch_fini(sb);
  printf("Test child started.\n");
}

static void get_child_thread(zx_handle_t channel, zx_handle_t* thread) {
  send_msg(channel, MSG_THREAD_HANDLE_REQUEST);
  Message msg;
  ASSERT_TRUE(recv_msg(channel, &msg));
  EXPECT_EQ(msg.type, MSG_THREAD_HANDLE_RESPONSE);
  EXPECT_EQ(msg.num_handles, 1u);
  *thread = msg.handles[0];
}

// Wait for |thread| to enter blocked state |reason|.
// We wait forever and let Unittest's watchdog handle errors.

static void wait_thread_blocked(zx_handle_t thread, uint32_t reason) {
  while (true) {
    zx_info_thread_t info;
    zx_status_t status =
        zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    ZX_ASSERT(status == ZX_OK);
    if (info.state == reason)
      break;
    zx_nanosleep(zx_deadline_after(THREAD_BLOCKED_WAIT_DURATION));
  }
}

// Terminate |process| by killing it and wait for it to exit.

static void terminate_process(zx_handle_t process) {
  zx_status_t status = zx_task_kill(process);
  ASSERT_EQ(status, ZX_OK);
  tu_process_wait_signaled(process);
}

// Note that ZX_THREAD_STATE_BLOCKED_EXCEPTION is tested in utest/exception
// but not here.  There's a lot of support logic and there's no reason to
// duplicate the coverage of it here.

TEST(ThreadStateTests, sleeping_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  send_msg(channel, MSG_SLEEP_TEST);

  // There is no good way to do this test without having the child
  // sleep forever and then kill it: There's no way to interrupt the sleep,
  // and there's no good value for the amount of time to sleep.
  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_SLEEPING);

  terminate_process(child);
}

TEST(ThreadStateTests, futex_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  send_msg_with_handles(channel, MSG_FUTEX_TEST, nullptr, 0);

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_FUTEX);

  terminate_process(child);
}

TEST(ThreadStateTests, port_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  zx_handle_t port;
  ASSERT_EQ(zx_port_create(0, &port), ZX_OK);
  zx_handle_t port_dupe = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_handle_duplicate(port, ZX_RIGHT_SAME_RIGHTS, &port_dupe), ZX_OK);

  send_msg_with_handles(channel, MSG_PORT_TEST, &port_dupe, 1);

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_PORT);

  // Wake the child up.
  EXPECT_EQ(zx_port_queue(port, &port_test_packet), ZX_OK);

  // The child sends a pass/fail message back as extra verification that
  // things went correctly on that side.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PASS));

  zx_handle_close(port);
  terminate_process(child);
}

TEST(ThreadStateTests, channel_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  zx_handle_t our_channel, their_channel;
  ASSERT_EQ(zx_channel_create(0, &our_channel, &their_channel), ZX_OK);

  send_msg_with_handles(channel, MSG_CHANNEL_TEST, &their_channel, 1);

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_CHANNEL);

  // Wake the child up.
  zx_handle_close(our_channel);

  // The child sends a pass/fail message back as extra verification that
  // things went correctly on that side.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PASS));

  terminate_process(child);
}

TEST(ThreadStateTests, wait_one_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  zx_handle_t h[2];
  ASSERT_EQ(zx_eventpair_create(0, &h[0], &h[1]), ZX_OK);

  send_msg_with_handles(channel, MSG_WAIT_ONE_TEST, &h[1], 1);

  // Don't continue until we see MSG_PROCEED, that tells us the child has
  // received the message and isn't in a wait_one/wait_many syscall.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PROCEED));

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  // Wake the child up.
  zx_handle_close(h[0]);

  // The child sends a pass/fail message back as extra verification that
  // things went correctly on that side.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PASS));

  terminate_process(child);
}

TEST(ThreadStateTests, wait_many_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  uint32_t num_handles = NUM_WAIT_MANY_HANDLES;
  zx_handle_t h[2][num_handles];
  for (uint32_t i = 0; i < num_handles; ++i) {
    ASSERT_EQ(zx_eventpair_create(0, &h[0][i], &h[1][i]), ZX_OK);
  }

  send_msg_with_handles(channel, MSG_WAIT_MANY_TEST, &h[1][0], num_handles);

  // Don't continue until we see MSG_PROCEED, that tells us the child has
  // received the message and isn't in a wait_one/wait_many syscall.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PROCEED));

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_WAIT_MANY);

  // Wake the child up.
  for (uint32_t i = 0; i < num_handles; ++i) {
    zx_handle_close(h[0][i]);
  }

  // The child sends a pass/fail message back as extra verification that
  // things went correctly on that side.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PASS));

  terminate_process(child);
}

// Just like wait_many_test except the child doesn't wait on any objects, just
// the (infinite) timeout.
TEST(ThreadStateTests, wait_many_no_objects_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  send_msg(channel, MSG_WAIT_MANY_TEST);

  // Don't continue until we see MSG_PROCEED, that tells us the child has
  // received the message and isn't in a wait_one/wait_many syscall.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PROCEED));

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_WAIT_MANY);

  // The child won't be sending a pass/fail message back because it's stuck in
  // object_wait_many so just kill it.
  terminate_process(child);
}

TEST(ThreadStateTests, interrupt_test) {
  // Start a new process.
  zx_handle_t child, channel;
  start_test_child(zx_job_default(), test_child_name, &child, &channel);
  zx_handle_t thread;
  ASSERT_NO_FATAL_FAILURES(get_child_thread(channel, &thread));

  zx_handle_t interrupt;
  // Creating a virtual interrupt does not require a valid handle.
  ASSERT_EQ(zx_interrupt_create(ZX_HANDLE_INVALID, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);
  zx_handle_t interrupt_dupe = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_handle_duplicate(interrupt, ZX_RIGHT_SAME_RIGHTS, &interrupt_dupe), ZX_OK);

  send_msg_with_handles(channel, MSG_INTERRUPT_TEST, &interrupt_dupe, 1);

  wait_thread_blocked(thread, ZX_THREAD_STATE_BLOCKED_INTERRUPT);

  // Wake the child up.
  EXPECT_EQ(zx_interrupt_trigger(interrupt, 0, interrupt_signaled_timestamp), ZX_OK);

  // The child sends a pass/fail message back as extra verification that
  // things went correctly on that side.
  ASSERT_NO_FATAL_FAILURES(recv_specific_msg(channel, MSG_PASS));

  zx_handle_close(interrupt);
  terminate_process(child);
}

int main(int argc, char** argv) {
  program_path = argv[0];

  if (argc >= 2) {
    if (strcmp(argv[1], test_child_name) == 0) {
      test_child();
      return 0;
    }
  }

  return zxtest::RunAllTests(argc, argv);
}
