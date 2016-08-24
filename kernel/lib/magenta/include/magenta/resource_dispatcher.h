// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <sys/types.h>

class ResourceDispatcher : public Dispatcher {
public:
    enum class Type {
        KERNEL,
    };

    static status_t Create(utils::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);

    virtual ~ResourceDispatcher() final;

    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_RESOURCE; }

    ResourceDispatcher* get_resource_dispatcher() final { return this; }

    virtual Type GetResourceType() const final { return Type::KERNEL; }

private:
    explicit ResourceDispatcher(void);

};