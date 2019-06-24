// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb {

// The interface configuration corresponding to a SuperSpeed device having one alt-interface.
struct alt_ss_config {
    // clang-format off
    usb_interface_descriptor_t  interface;
    usb_endpoint_descriptor_t   ep1;
    usb_ss_ep_comp_descriptor_t ss_companion1;
    usb_endpoint_descriptor_t   ep2;
    usb_ss_ep_comp_descriptor_t ss_companion2;
    usb_interface_descriptor_t  alt_interface;
    // clang-format on
};

// Taken from a real UMS-class device.
constexpr alt_ss_config kTestInterface = {
    .interface = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = 8,
        .bInterfaceSubClass = 6,
        .bInterfaceProtocol = 80,
        .iInterface = 0,
    },
    .ep1 = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x81,
        .bmAttributes = 2,
        .wMaxPacketSize = 1024,
        .bInterval = 0,
    },
    .ss_companion1 = {
        .bLength = sizeof(usb_ss_ep_comp_descriptor_t),
        .bDescriptorType = USB_DT_SS_EP_COMPANION,
        .bMaxBurst = 3,
        .bmAttributes = 0,
        .wBytesPerInterval = 0,
    },
    .ep2 = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2,
        .bmAttributes = 2,
        .wMaxPacketSize = 1024,
        .bInterval = 0,
    },
    .ss_companion2 = {
        .bLength = sizeof(usb_ss_ep_comp_descriptor_t),
        .bDescriptorType = USB_DT_SS_EP_COMPANION,
        .bMaxBurst = 3,
        .bmAttributes = 0,
        .wBytesPerInterval = 0,
    },
    .alt_interface = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 1,
        .bNumEndpoints = 2,
        .bInterfaceClass = 8,
        .bInterfaceSubClass = 6,
        .bInterfaceProtocol = 80,
        .iInterface = 0,
    },
};

class WrapperTest : public zxtest::Test {
protected:
    void SetUp() {
        usb_protocol_t proto{&ops_, this};
        ops_.get_descriptors_length = UsbGetDescriptorsLength;
        ops_.get_descriptors = UsbGetDescriptors;
        usb_ = ddk::UsbProtocolClient(&proto);
    }

    static void UsbGetDescriptors(void* ctx, void* out_descs_buffer, size_t descs_size,
                                  size_t* out_descs_actual) {
        memcpy(out_descs_buffer, &kTestInterface, descs_size);
        *out_descs_actual = descs_size;
    }

    static size_t UsbGetDescriptorsLength(void* ctx) { return sizeof(kTestInterface); }

    usb_protocol_ops_t ops_{};
    ddk::UsbProtocolClient usb_;
};

TEST_F(WrapperTest, TestInterfaceRangeIterationSkippingAlt) {
    // This tests that for(x : y) syntax produces the correct interface descriptors.
    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    int count = 0;
    for (auto& interface : *ilist) {
        EXPECT_TRUE(count++ < 1);
        auto& want = kTestInterface.interface;
        EXPECT_EQ(0, memcmp(&want, interface.descriptor(), sizeof(want)));
    };
}

TEST_F(WrapperTest, TestInterfaceRangeIterationNotSkippingAlt) {
    // This tests that for(x : y) syntax produces the correct interface descriptors.
    const usb_interface_descriptor_t wants[2] = {
        kTestInterface.interface,
        kTestInterface.alt_interface,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

    unsigned int count = 0;
    for (auto& interface : *ilist) {
        EXPECT_TRUE(count < countof(wants));
        auto& want = wants[count++];
        EXPECT_EQ(0, memcmp(&want, interface.descriptor(), sizeof(want)));
    }
}

TEST_F(WrapperTest, TestEndpointRangeIteration) {
    // This tests that for(x : y) syntax produces the correct endpoint descriptors.
    const usb_endpoint_descriptor_t wants[2] = {
        kTestInterface.ep1,
        kTestInterface.ep2,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    unsigned int count = 0;
    for (auto& interface : *ilist) {
        for (auto& ep : interface) {
            EXPECT_TRUE(count < countof(wants));
            auto& want = wants[count++];
            EXPECT_EQ(0, memcmp(&want, &ep, sizeof(want)));
        }
    }
}

TEST_F(WrapperTest, TestInterfaceAccessOps) {
    // This tests the various Interface access ops of a InterfaceList::iterator.
    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->begin();
    int count = 0;
    do {
        EXPECT_TRUE(count++ < 1);
        auto& want = kTestInterface.interface;
        EXPECT_EQ(0, memcmp(&want, itr->descriptor(), sizeof(want)));
        EXPECT_EQ(0, memcmp(&want, (*itr).descriptor(), sizeof(want)));
        EXPECT_EQ(0, memcmp(&want, itr.get()->descriptor(), sizeof(want)));
    } while (++itr != ilist->end());
}

TEST_F(WrapperTest, TestEndpointAccessOps) {
    // This tests the various endpoint descriptor ops of an Interface::iterator.
    const usb_endpoint_descriptor_t wants[2] = {
        kTestInterface.ep1,
        kTestInterface.ep2,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->begin();
    unsigned int count = 0;
    do {
        auto ep_itr = itr->begin();
        do {
            EXPECT_TRUE(count < countof(wants));
            auto& want = wants[count++];
            EXPECT_EQ(0, memcmp(&want, ep_itr.endpoint(), sizeof(want)));
            EXPECT_EQ(0, memcmp(&want, &(*ep_itr), sizeof(want)));
            EXPECT_EQ(want.bEndpointAddress, ep_itr->bEndpointAddress);
        } while (++ep_itr != itr->end());
    } while (++itr != ilist->end());
}

TEST_F(WrapperTest, TestInterfaceIterationSkippingAlt) {
    // This tests that the iterator syntax produces the correct interface descriptors.
    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->begin();
    int count = 0;
    do {
        EXPECT_TRUE(count++ < 1);
        auto& want = kTestInterface.interface;
        EXPECT_EQ(0, memcmp(&want, itr->descriptor(), sizeof(want)));
    } while (++itr != ilist->end());
}

TEST_F(WrapperTest, TestInterfaceIterationNotSkippingAlt) {
    // This tests that the iterator syntax produces the correct interface descriptors.
    const usb_interface_descriptor_t wants[2] = {
        kTestInterface.interface,
        kTestInterface.alt_interface,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

    auto itr = ilist->begin();
    unsigned int count = 0;
    do {
        EXPECT_TRUE(count < countof(wants));
        auto& want = wants[count++];
        EXPECT_EQ(0, memcmp(&want, itr->descriptor(), sizeof(want)));
    } while (++itr != ilist->end());
}

TEST_F(WrapperTest, TestEndpointIteration) {
    // This tests that the iterator syntax produces the correct endpoint descriptors.
    const usb_endpoint_descriptor_t wants[2] = {
        kTestInterface.ep1,
        kTestInterface.ep2,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->begin();
    unsigned int count = 0;
    do {
        auto ep_itr = itr->begin();
        do {
            EXPECT_TRUE(count < countof(wants));
            auto& want = wants[count++];
            EXPECT_EQ(0, memcmp(&want, ep_itr.endpoint(), sizeof(want)));
        } while (++ep_itr != itr->end());
    } while (++itr != ilist->end());
}

TEST_F(WrapperTest, TestInterfaceConstIterationSkippingAlt) {
    // This tests that the const_iterator syntax produces the correct interface descriptors.
    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->cbegin();
    int count = 0;
    do {
        EXPECT_TRUE(count++ < 1);
        auto& want = kTestInterface.interface;
        EXPECT_EQ(0, memcmp(&want, itr->descriptor(), sizeof(want)));
    } while (++itr != ilist->cend());
}

TEST_F(WrapperTest, TestInterfaceConstIterationNotSkippingAlt) {
    // This tests that the const_iterator syntax produces the correct interface descriptors.
    const usb_interface_descriptor_t wants[2] = {
        kTestInterface.interface,
        kTestInterface.alt_interface,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

    auto itr = ilist->cbegin();
    unsigned int count = 0;
    do {
        EXPECT_TRUE(count < countof(wants));
        auto& want = wants[count++];
        EXPECT_EQ(0, memcmp(&want, itr->descriptor(), sizeof(want)));
    } while (++itr != ilist->cend());
}

TEST_F(WrapperTest, TestEndpointConstIteration) {
    // This tests that the const_iterator syntax produces the correct endpoint descriptors.
    const usb_endpoint_descriptor_t wants[2] = {
        kTestInterface.ep1,
        kTestInterface.ep2,
    };

    std::optional<InterfaceList> ilist;
    ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

    auto itr = ilist->cbegin();
    unsigned int count = 0;
    do {
        auto ep_itr = itr->begin();
        do {
            EXPECT_TRUE(count < countof(wants));
            auto& want = wants[count++];
            EXPECT_EQ(0, memcmp(&want, ep_itr.endpoint(), sizeof(want)));
        } while (++ep_itr != itr->end());
    } while (++itr != ilist->cend());
}

} // namespace usb

int main(int argc, char* argv[]) {
    return RUN_ALL_TESTS(argc, argv);
}
