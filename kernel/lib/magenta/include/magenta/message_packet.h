// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/magenta.h>

#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

struct MessagePacket : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<MessagePacket>> {
    MessagePacket(mxtl::Array<uint8_t>&& _data, mxtl::Array<Handle*>&& _handles)
        : data(mxtl::move(_data)), handles(mxtl::move(_handles)) {}
    ~MessagePacket() {
        for (size_t ix = 0; ix != handles.size(); ++ix) {
            DeleteHandle(handles[ix]);
        }
    }

    mxtl::Array<uint8_t> data;
    mxtl::Array<Handle*> handles;

    void ReturnHandles() { handles.reset(); }
};
