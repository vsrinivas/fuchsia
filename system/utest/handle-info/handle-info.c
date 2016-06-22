// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

#define CHECK(f, expected, message)                                 \
    do {                                                            \
        mx_status_t ret = (f);                                                  \
        char msg[32];                                               \
        snprintf(msg, sizeof(msg), "Test failed (%s): " #f " returned %d vs. %d\n",     \
                   message, (int)ret, (int)expected);               \
        EXPECT_EQ(ret, (int)(expected), msg);                            \
        if ((ret = (f)) != (expected)) {                            \
            return __LINE__;                                        \
        }                                                           \
    } while (0)

bool handle_info_test(void) {
    BEGIN_TEST;

    mx_handle_t event = _magenta_event_create(0u);
    mx_handle_t duped = _magenta_handle_duplicate(event);

    CHECK(_magenta_handle_get_info(
              event, MX_INFO_HANDLE_VALID, NULL, 0u),
          NO_ERROR, "handle should be valid");
    CHECK(_magenta_handle_close(event), NO_ERROR, "failed to close the handle");
    CHECK(_magenta_handle_get_info(
              event, MX_INFO_HANDLE_VALID, NULL, 0u),
          ERR_BAD_HANDLE, "handle should be valid");

    handle_basic_info_t info = {0};
    CHECK(_magenta_handle_get_info(
              duped, MX_INFO_HANDLE_BASIC, &info, 4u),
          ERR_NOT_ENOUGH_BUFFER, "bad struct size validation");

    CHECK(_magenta_handle_get_info(
              duped, MX_INFO_HANDLE_BASIC, &info, sizeof(info)),
          sizeof(info), "handle should be valid");

    const mx_rights_t evr = MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
                            MX_RIGHT_READ | MX_RIGHT_WRITE;

    if (info.type != MX_OBJ_TYPE_EVENT)
        CHECK(0, 1, "handle should be an event");
    if (info.rights != evr)
        CHECK(0, 1, "wrong set of rights");

    unittest_printf("Done\n");
    END_TEST;
}

BEGIN_TEST_CASE(handle_info_tests)
RUN_TEST(handle_info_test)
END_TEST_CASE(handle_info_tests)

int main(void) {
    // TODO: remove this register once global constructors work
    unittest_register_test_case(&_handle_info_tests_element);
    return unittest_run_all_tests() ? 0 : -1;
}
