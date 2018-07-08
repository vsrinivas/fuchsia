// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/inflight_list.h"
#include "gtest/gtest.h"
#include <lib/fdio/io.h>
#include <zx/channel.h>

struct TestConnection : public magma_connection_t {
    TestConnection()
    {
        zx::channel::create(0, &channel[0], &channel[1]);
        fd = fdio_handle_fd(channel[0].get(), ZX_CHANNEL_READABLE, 0, true);
    }
    ~TestConnection() { close(fd); }

    zx::channel channel[2];
    int fd;
};

extern "C" {

int32_t magma_get_notification_channel_fd(struct magma_connection_t* connection)
{
    return static_cast<TestConnection*>(connection)->fd;
}

magma_status_t magma_read_notification_channel(struct magma_connection_t* connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out)
{
    uint32_t buffer_actual_size;
    zx_status_t status =
        static_cast<TestConnection*>(connection)
            ->channel[0]
            .read(0, buffer, buffer_size, &buffer_actual_size, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *buffer_size_out = buffer_actual_size;
        return MAGMA_STATUS_OK;
    }
    return MAGMA_STATUS_INTERNAL_ERROR;
}
}

// Read a notification from the channel into |buffer|. Sets |*buffer_size_out| to 0 if there are no
// messages pending.
magma_status_t magma_read_notification_channel(struct magma_connection_t* connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out);

TEST(MagmaUtil, InflightList)
{
    TestConnection connection;
    magma::InflightList list(&connection);

    uint64_t buffer_id = 0xabab1234;
    EXPECT_FALSE(list.is_inflight(buffer_id));
    list.add(buffer_id);
    EXPECT_TRUE(list.is_inflight(buffer_id));

    EXPECT_FALSE(list.WaitForCompletion(100));
    connection.channel[1].write(0, &buffer_id, sizeof(buffer_id), nullptr, 0);
    EXPECT_TRUE(list.WaitForCompletion(100));

    list.ServiceCompletions(&connection);
    EXPECT_FALSE(list.is_inflight(buffer_id));
}
