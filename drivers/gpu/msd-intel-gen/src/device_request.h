// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_REQUEST_H
#define DEVICE_REQUEST_H

#include "platform_event.h"
#include <memory>

class MsdIntelDevice;

class DeviceRequest {
public:
    std::shared_ptr<magma::PlatformEvent> GetReply()
    {
        if (!reply_event_)
            reply_event_ = std::shared_ptr<magma::PlatformEvent>(magma::PlatformEvent::Create());
        return reply_event_;
    }

    void ProcessAndReply(MsdIntelDevice* device)
    {
        Process(device);

        if (reply_event_)
            reply_event_->Signal();
    }

protected:
    virtual void Process(MsdIntelDevice* device) {}

private:
    std::shared_ptr<magma::PlatformEvent> reply_event_;
};

#endif
