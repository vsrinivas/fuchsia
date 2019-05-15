// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <zircon/status.h>
#include <zxtest/zxtest.h>

namespace {

TEST(StatusString, AllOfEm) {
    EXPECT_STR_EQ(zx_status_get_string(ZX_OK), "ZX_OK");                               // (0)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_INTERNAL), "ZX_ERR_INTERNAL");           // (-1)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_SUPPORTED), "ZX_ERR_NOT_SUPPORTED"); // (-2)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NO_RESOURCES), "ZX_ERR_NO_RESOURCES");   //  (-3)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NO_MEMORY), "ZX_ERR_NO_MEMORY");         //  (-4)
    EXPECT_STR_EQ(zx_status_get_string(-5), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_INTERNAL_INTR_RETRY),
                  "ZX_ERR_INTERNAL_INTR_RETRY"); //  (-6)
    EXPECT_STR_EQ(zx_status_get_string(-7), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-8), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-9), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_INVALID_ARGS), "ZX_ERR_INVALID_ARGS"); //  (-10)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_BAD_HANDLE), "ZX_ERR_BAD_HANDLE");     //  (-11)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_WRONG_TYPE), "ZX_ERR_WRONG_TYPE");     //  (-12)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_BAD_SYSCALL), "ZX_ERR_BAD_SYSCALL");   //  (-13)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_OUT_OF_RANGE), "ZX_ERR_OUT_OF_RANGE"); //  (-14)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_BUFFER_TOO_SMALL),
                  "ZX_ERR_BUFFER_TOO_SMALL"); //  (-15)
    EXPECT_STR_EQ(zx_status_get_string(-16), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-17), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-18), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-19), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_BAD_STATE), "ZX_ERR_BAD_STATE");           //  (-20)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_TIMED_OUT), "ZX_ERR_TIMED_OUT");           //  (-21)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_SHOULD_WAIT), "ZX_ERR_SHOULD_WAIT");       //  (-22)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_CANCELED), "ZX_ERR_CANCELED");             //  (-23)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_PEER_CLOSED), "ZX_ERR_PEER_CLOSED");       //  (-24)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_FOUND), "ZX_ERR_NOT_FOUND");           //  (-25)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ALREADY_EXISTS), "ZX_ERR_ALREADY_EXISTS"); //  (-26)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ALREADY_BOUND), "ZX_ERR_ALREADY_BOUND");   //  (-27)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_UNAVAILABLE), "ZX_ERR_UNAVAILABLE");       //  (-28)
    EXPECT_STR_EQ(zx_status_get_string(-29), "(UNKNOWN)");                               //  (-29)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ACCESS_DENIED), "ZX_ERR_ACCESS_DENIED");   //  (-30)
    EXPECT_STR_EQ(zx_status_get_string(-31), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-32), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-33), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-34), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-35), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-36), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-37), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-38), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-39), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO), "ZX_ERR_IO");                 //  (-40)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_REFUSED), "ZX_ERR_IO_REFUSED"); //  (-41)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_DATA_INTEGRITY),
                  "ZX_ERR_IO_DATA_INTEGRITY");                                           //  (-42)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_DATA_LOSS), "ZX_ERR_IO_DATA_LOSS");     //  (-43)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_NOT_PRESENT), "ZX_ERR_IO_NOT_PRESENT"); //  (-44)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_OVERRUN), "ZX_ERR_IO_OVERRUN");         //  (-45)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_MISSED_DEADLINE),
                  "ZX_ERR_IO_MISSED_DEADLINE");                                  //  (-46)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_IO_INVALID), "ZX_ERR_IO_INVALID"); //  (-47)
    EXPECT_STR_EQ(zx_status_get_string(-48), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-49), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_BAD_PATH), "ZX_ERR_BAD_PATH");   //  (-50)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_DIR), "ZX_ERR_NOT_DIR");     //  (-51)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_FILE), "ZX_ERR_NOT_FILE");   //  (-52)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_FILE_BIG), "ZX_ERR_FILE_BIG");   //  (-53)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NO_SPACE), "ZX_ERR_NO_SPACE");   //  (-54)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_EMPTY), "ZX_ERR_NOT_EMPTY"); //  (-55)
    EXPECT_STR_EQ(zx_status_get_string(-56), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-57), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-58), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-59), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_STOP), "ZX_ERR_STOP");   //  (-60)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NEXT), "ZX_ERR_NEXT");   //  (-61)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ASYNC), "ZX_ERR_ASYNC"); //  (-62)
    EXPECT_STR_EQ(zx_status_get_string(-63), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-64), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-65), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-66), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-67), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-68), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(-69), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_PROTOCOL_NOT_SUPPORTED),
                  "ZX_ERR_PROTOCOL_NOT_SUPPORTED"); //  (-70)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ADDRESS_UNREACHABLE),
                  "ZX_ERR_ADDRESS_UNREACHABLE");                                         //  (-71)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_ADDRESS_IN_USE), "ZX_ERR_ADDRESS_IN_USE"); //  (-72)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_NOT_CONNECTED), "ZX_ERR_NOT_CONNECTED");   //  (-73)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_CONNECTION_REFUSED),
                  "ZX_ERR_CONNECTION_REFUSED"); //  (-74)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_CONNECTION_RESET),
                  "ZX_ERR_CONNECTION_RESET"); //  (-75)
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_CONNECTION_ABORTED),
                  "ZX_ERR_CONNECTION_ABORTED"); //  (-76)

    // One past our current last-known should be unknown (note that all errors
    // are negative, so -1, not +1).
    ASSERT_NOT_NULL(zx_status_get_string(ZX_ERR_CONNECTION_ABORTED - 1));
    EXPECT_STR_EQ(zx_status_get_string(ZX_ERR_CONNECTION_ABORTED - 1), "(UNKNOWN)");

    EXPECT_STR_EQ(zx_status_get_string(INT32_MIN), "(UNKNOWN)");
    EXPECT_STR_EQ(zx_status_get_string(INT32_MAX), "(UNKNOWN)");
}

} // namespace
