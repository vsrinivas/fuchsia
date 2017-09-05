// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

struct AsyncStub : public async_t {
public:
    AsyncStub();
    virtual ~AsyncStub();

    virtual mx_status_t BeginWait(async_wait_t* wait);
    virtual mx_status_t CancelWait(async_wait_t* wait);
    virtual mx_status_t PostTask(async_task_t* task);
    virtual mx_status_t CancelTask(async_task_t* task);
    virtual mx_status_t QueuePacket(async_receiver_t* receiver,
                                    const mx_packet_user_t* data);
};
