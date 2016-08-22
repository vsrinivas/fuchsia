// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "msd.h"
#include "msd_intel_context.h"
#include <memory>

class MsdIntelDevice;

class MsdIntelConnection : public msd_connection {
public:
    MsdIntelConnection() { magic_ = kMagic; }
    virtual ~MsdIntelConnection() {}

    std::unique_ptr<MsdIntelContext> CreateContext()
    {
        return std::unique_ptr<MsdIntelContext>(new MsdIntelContext());
    }

    static MsdIntelConnection* cast(msd_connection* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdIntelConnection*>(connection);
    }

private:
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};