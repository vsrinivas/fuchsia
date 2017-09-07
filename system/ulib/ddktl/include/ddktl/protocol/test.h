// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/test.h>
#include <magenta/assert.h>
#include <mx/channel.h>
#include <mx/socket.h>

// DDK test protocol support
//
// :: Proxy ::
//
// ddk::TestProtocolProxy is a simple wrapper around test_protocol_t. It does not own the pointers
// passed to it.
//
// :: Mixin ::
//
// No mixins are defined, as it is not expected that there will be multiple implementations of the
// test protocol.
//
// :: Example ::
//
// // A driver that communicates with a MX_PROTOCOL_TEST device
// class MyDevice;
// using MyDeviceType = ddk::Device<MyDevice, /* ddk mixins */>;
//
// static mx_status_t my_test_func(void* cookie, test_report_t* report, const void* arg,
//                                 size_t arglen) {
//     auto dev = static_cast<MyDevice*>(cookie);
//     // run tests and set up report
//     return MX_OK;
// }
//
// class MyDevice : public MyDeviceType {
//   public:
//     MyDevice(mx_device_t* parent)
//       : MyDeviceType("my-device"),
//         parent_(parent) {}
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     mx_status_t Bind() {
//         test_protocol_t* ops;
//         auto status = get_device_protocol(parent_, MX_PROTOCOL_TEST,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != MX_OK) {
//             return status;
//         }
//        proxy_.reset(new ddk::TestProtocolProxy(ops, parent_));
//
//        // Set up the test
//        proxy_->SetTestFunc(my_test_func, this);
//        return Add(parent_);
//     }
//
//   private:
//     fbl::unique_ptr<ddk::TestProtocolProxy> proxy_;
// };

namespace ddk {

class TestProtocolProxy {
  public:
    TestProtocolProxy(test_protocol_t* proto)
      : ops_(proto->ops), ctx_(proto->ctx) {}

    void SetOutputSocket(mx::socket socket) {
        ops_->set_output_socket(ctx_, socket.release());
    }

    mx::socket GetOutputSocket() {
        return mx::socket(ops_->get_output_socket(ctx_));
    }

    void SetControlChannel(mx::channel chan) {
        ops_->set_control_channel(ctx_, chan.release());
    }

    mx::channel GetControlChannel() {
        return mx::channel(ops_->get_control_channel(ctx_));
    }

    void SetTestFunc(test_func_t func, void* cookie) {
        ops_->set_test_func(ctx_, func, cookie);
    }

    mx_status_t RunTests(test_report_t* report, const void* arg, size_t arglen) {
        return ops_->run_tests(ctx_, report, arg, arglen);
    }

    void Destroy() {
        ops_->destroy(ctx_);
    }

  private:
    test_protocol_ops_t* ops_;
    void* ctx_;

};

}  // namespace ddk
