// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/inflight_list.h"
#include "gtest/gtest.h"
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>

struct TestConnection : public magma_connection {
    TestConnection() { zx::channel::create(0, &channel[0], &channel[1]); }

    zx::channel channel[2];
};

extern "C" {

magma_status_t magma_wait_notification_channel(magma_connection_t connection, int64_t timeout_ns)
{
    zx_signals_t pending;
    zx_status_t status =
        static_cast<TestConnection*>(connection)
            ->channel[0]
            .wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::nsec(timeout_ns)), &pending);
    if (status != ZX_OK)
        return DRET(MAGMA_STATUS_INTERNAL_ERROR);
    DASSERT(pending & ZX_CHANNEL_READABLE);
    return MAGMA_STATUS_OK;
}

magma_status_t magma_read_notification_channel(magma_connection_t connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out)
{
    uint32_t buffer_actual_size;
    zx_status_t status =
        static_cast<TestConnection*>(connection)
            ->channel[0]
            .rea2(0, buffer, nullptr, buffer_size, 0, &buffer_actual_size, nullptr);
    if (status == ZX_OK) {
        *buffer_size_out = buffer_actual_size;
        return MAGMA_STATUS_OK;
    }
    return MAGMA_STATUS_INTERNAL_ERROR;
}
}

TEST(MagmaUtil, InflightList)
{
    TestConnection connection;
    magma::InflightList list;

    uint64_t buffer_id = 0xabab1234;
    EXPECT_FALSE(list.is_inflight(buffer_id));
    list.add(buffer_id);
    EXPECT_TRUE(list.is_inflight(buffer_id));

    EXPECT_FALSE(list.WaitForCompletion(&connection, 100));
    connection.channel[1].write(0, &buffer_id, sizeof(buffer_id), nullptr, 0);
    EXPECT_TRUE(list.WaitForCompletion(&connection, 100));

    list.ServiceCompletions(&connection);
    EXPECT_FALSE(list.is_inflight(buffer_id));
}
