// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/exceptionate.h>

#include <zircon/errors.h>

zx_status_t Exceptionate::SetChannel(fbl::RefPtr<ChannelDispatcher> channel) {
    if (!channel) {
        return ZX_ERR_INVALID_ARGS;
    }

    Guard<fbl::Mutex> guard{&lock_};

    if (channel_ && !channel_->PeerHasClosed()) {
        return ZX_ERR_ALREADY_BOUND;
    }

    // At this point we're certain that either there is no channel or it's a
    // dead channel with no peer (since channel endpoints can't re-open) so we
    // can overwrite it.
    channel_ = ktl::move(channel);

    return ZX_OK;
}
