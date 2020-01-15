// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

static bool get_rights(zx_handle_t handle, zx_rights_t* rights) {
  zx_info_handle_basic_t info;
  ASSERT_EQ(zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL),
            ZX_OK, "");
  *rights = info.rights;
  return true;
}

static bool get_new_rights(zx_handle_t handle, zx_rights_t new_rights, zx_handle_t* new_handle) {
  ASSERT_EQ(zx_handle_duplicate(handle, new_rights, new_handle), ZX_OK, "");
  return true;
}

// |object| must have ZX_RIGHT_{GET,SET}_PROPERTY.

static bool test_name_property(zx_handle_t object) {
  char set_name[ZX_MAX_NAME_LEN];
  char get_name[ZX_MAX_NAME_LEN];

  // name with extra garbage at the end
  memset(set_name, 'A', sizeof(set_name));
  set_name[1] = '\0';

  EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME, set_name, sizeof(set_name)), ZX_OK, "");
  EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME, get_name, sizeof(get_name)), ZX_OK, "");
  EXPECT_EQ(get_name[0], 'A', "");
  for (size_t i = 1; i < sizeof(get_name); i++) {
    EXPECT_EQ(get_name[i], '\0', "");
  }

  // empty name
  strcpy(set_name, "");
  EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME, set_name, strlen(set_name)), ZX_OK, "");
  EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME, get_name, sizeof(get_name)), ZX_OK, "");
  EXPECT_EQ(strcmp(get_name, set_name), 0, "");

  // largest possible name
  memset(set_name, 'x', sizeof(set_name) - 1);
  set_name[sizeof(set_name) - 1] = '\0';
  EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME, set_name, strlen(set_name)), ZX_OK, "");
  EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME, get_name, sizeof(get_name)), ZX_OK, "");
  EXPECT_EQ(strcmp(get_name, set_name), 0, "");

  // too large a name by 1
  memset(set_name, 'x', sizeof(set_name));
  EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME, set_name, sizeof(set_name)), ZX_OK, "");

  zx_rights_t current_rights;
  if (get_rights(object, &current_rights)) {
    zx_rights_t cant_set_rights = current_rights &= ~ZX_RIGHT_SET_PROPERTY;
    zx_handle_t cant_set;
    if (get_new_rights(object, cant_set_rights, &cant_set)) {
      EXPECT_EQ(zx_object_set_property(cant_set, ZX_PROP_NAME, "", 0), ZX_ERR_ACCESS_DENIED, "");
      zx_handle_close(cant_set);
    }
  }

  return true;
}

static bool job_name_test(void) {
  BEGIN_TEST;

  zx_handle_t testjob;
  zx_status_t s = zx_job_create(zx_job_default(), 0, &testjob);
  EXPECT_EQ(s, ZX_OK, "");

  bool success = test_name_property(testjob);
  if (!success)
    return false;

  zx_handle_close(testjob);
  END_TEST;
}

static bool process_name_test(void) {
  BEGIN_TEST;

  zx_handle_t self = zx_process_self();
  bool success = test_name_property(self);
  if (!success)
    return false;

  END_TEST;
}

static bool thread_name_test(void) {
  BEGIN_TEST;

  zx_handle_t main_thread = thrd_get_zx_handle(thrd_current());
  unittest_printf("thread handle %d\n", main_thread);
  bool success = test_name_property(main_thread);
  if (!success)
    return false;

  END_TEST;
}

static bool vmo_name_test(void) {
  BEGIN_TEST;

  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(16, 0u, &vmo), ZX_OK, "");
  unittest_printf("VMO handle %d\n", vmo);

  char name[ZX_MAX_NAME_LEN];
  memset(name, 'A', sizeof(name));

  // Name should start out empty.
  EXPECT_EQ(zx_object_get_property(vmo, ZX_PROP_NAME, name, sizeof(name)), ZX_OK, "");
  for (size_t i = 0; i < sizeof(name); i++) {
    EXPECT_EQ(name[i], '\0', "");
  }

  // Check the rest.
  bool success = test_name_property(vmo);
  if (!success)
    return false;

  END_TEST;
}

static bool socket_buffer_test(void) {
  BEGIN_TEST;

  zx_handle_t sockets[2];
  ASSERT_EQ(zx_socket_create(0, &sockets[0], &sockets[1]), ZX_OK, "");

  // Check the buffer size after a write.
  uint8_t buf[8] = {};
  size_t actual;
  ASSERT_EQ(zx_socket_write(sockets[1], 0, buf, sizeof(buf), &actual), ZX_OK, "");
  EXPECT_EQ(actual, sizeof(buf), "");

  zx_info_socket_t info;

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[0], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, 0u, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, sizeof(buf), "");
  EXPECT_EQ(info.rx_buf_available, sizeof(buf), "");
  EXPECT_GT(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, 0u, "");

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[1], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, 0u, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, 0u, "");
  EXPECT_EQ(info.rx_buf_available, 0u, "");
  EXPECT_GT(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, sizeof(buf), "");

  // Check TX buf goes to zero on peer closed.
  zx_handle_close(sockets[0]);

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[1], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, 0u, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, 0u, "");
  EXPECT_EQ(info.rx_buf_available, 0u, "");
  EXPECT_EQ(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, 0u, "");

  zx_handle_close(sockets[1]);

  ASSERT_EQ(zx_socket_create(ZX_SOCKET_DATAGRAM, &sockets[0], &sockets[1]), ZX_OK, "");

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[0], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, ZX_SOCKET_DATAGRAM, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, 0u, "");
  EXPECT_EQ(info.rx_buf_available, 0u, "");
  EXPECT_GT(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, 0u, "");

  ASSERT_EQ(zx_socket_write(sockets[1], 0, buf, sizeof(buf), &actual), ZX_OK, "");
  EXPECT_EQ(actual, sizeof(buf), "");

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[0], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, ZX_SOCKET_DATAGRAM, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, 8u, "");
  EXPECT_EQ(info.rx_buf_available, 8u, "");
  EXPECT_GT(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, 0u, "");

  ASSERT_EQ(zx_socket_write(sockets[1], 0, buf, sizeof(buf) / 2, &actual), ZX_OK, "");
  EXPECT_EQ(actual, sizeof(buf) / 2, "");

  memset(&info, 0, sizeof(info));
  ASSERT_EQ(zx_object_get_info(sockets[0], ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL), ZX_OK,
            "");
  EXPECT_EQ(info.options, ZX_SOCKET_DATAGRAM, "");
  EXPECT_GT(info.rx_buf_max, 0u, "");
  EXPECT_EQ(info.rx_buf_size, 12u, "");
  EXPECT_EQ(info.rx_buf_available, 8u, "");
  EXPECT_GT(info.tx_buf_max, 0u, "");
  EXPECT_EQ(info.tx_buf_size, 0u, "");

  zx_handle_close_many(sockets, 2);

  END_TEST;
}

#if defined(__x86_64__)

static uintptr_t read_gs(void) {
  uintptr_t gs;
  __asm__ __volatile__("mov %%gs:0,%0" : "=r"(gs));
  return gs;
}

static int do_nothing(void* unused) {
  for (;;) {
  }
  return 0;
}

static bool fs_invalid_test(void) {
  BEGIN_TEST;

  // The success case of fs is hard to explicitly test, but is
  // exercised all the time (ie userspace would explode instantly if
  // it was broken). Since we will be soon adding a corresponding
  // mechanism for gs, don't worry about testing success.

  uintptr_t fs_storage;
  uintptr_t fs_location = (uintptr_t)&fs_storage;

  // All the failures:

  // Try a thread other than the current one.
  thrd_t t;
  int success = thrd_create(&t, &do_nothing, NULL);
  ASSERT_EQ(success, thrd_success, "");
  zx_handle_t other_thread = thrd_get_zx_handle(t);
  zx_status_t status =
      zx_object_set_property(other_thread, ZX_PROP_REGISTER_FS, &fs_location, sizeof(fs_location));
  ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

  // Try a non-thread object type.
  status = zx_object_set_property(zx_process_self(), ZX_PROP_REGISTER_FS, &fs_location,
                                  sizeof(fs_location));
  ASSERT_EQ(status, ZX_ERR_WRONG_TYPE, "");

  // Not enough buffer to hold the property value.
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS, &fs_location,
                                  sizeof(fs_location) - 1);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "");

  // A non-canonical vaddr.
  uintptr_t noncanonical_fs_location = fs_location | (1ull << 47);
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS, &noncanonical_fs_location,
                                  sizeof(noncanonical_fs_location));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

  // A non-userspace vaddr.
  uintptr_t nonuserspace_fs_location = 0xffffffff40000000;
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS, &nonuserspace_fs_location,
                                  sizeof(nonuserspace_fs_location));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

  END_TEST;
}

static bool gs_test(void) {
  BEGIN_TEST;

  // First test the success case.
  const uintptr_t expected = 0xfeedfacefeedface;

  uintptr_t gs_storage = expected;
  uintptr_t gs_location = (uintptr_t)&gs_storage;

  zx_status_t status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS, &gs_location,
                                              sizeof(gs_location));
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(read_gs(), expected, "");

  // All the failures:

  // Try a thread other than the current one.
  thrd_t t;
  int success = thrd_create(&t, &do_nothing, NULL);
  ASSERT_EQ(success, thrd_success, "");
  zx_handle_t other_thread = thrd_get_zx_handle(t);
  status =
      zx_object_set_property(other_thread, ZX_PROP_REGISTER_GS, &gs_location, sizeof(gs_location));
  ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

  // Try a non-thread object type.
  status = zx_object_set_property(zx_process_self(), ZX_PROP_REGISTER_GS, &gs_location,
                                  sizeof(gs_location));
  ASSERT_EQ(status, ZX_ERR_WRONG_TYPE, "");

  // Not enough buffer to hold the property value.
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS, &gs_location,
                                  sizeof(gs_location) - 1);
  ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "");

  // A non-canonical vaddr.
  uintptr_t noncanonical_gs_location = gs_location | (1ull << 47);
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS, &noncanonical_gs_location,
                                  sizeof(noncanonical_gs_location));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

  // A non-userspace vaddr.
  uintptr_t nonuserspace_gs_location = 0xffffffff40000000;
  status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS, &nonuserspace_gs_location,
                                  sizeof(nonuserspace_gs_location));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

  END_TEST;
}

#endif  // defined(__x86_64__)

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(job_name_test);
RUN_TEST(vmo_name_test);
RUN_TEST(socket_buffer_test);
#if defined(__x86_64__)
RUN_TEST(fs_invalid_test)
RUN_TEST(gs_test)
#endif
END_TEST_CASE(property_tests)
