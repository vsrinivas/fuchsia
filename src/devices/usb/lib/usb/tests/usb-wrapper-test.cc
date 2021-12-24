// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/hidbus/c/banjo.h>
#include <zircon/hw/usb/hid.h>

#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb {

typedef struct {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdHID;
  uint8_t bCountryCode;
  uint8_t bNumDescriptors;
} __attribute__((packed)) usb_hid_descriptor_for_test_t;

// The interface configuration corresponding to a HighSpeed device having one alt-interface.
typedef struct {
  // clang-format off
    usb_interface_descriptor_t interface;
    usb_endpoint_descriptor_t  ep1;
    usb_endpoint_descriptor_t  ep2;
    usb_hid_descriptor_for_test_t hid_descriptor;
    usb_interface_descriptor_t alt_interface;
  // clang-format on
} alt_hs_config;

// The interface configuration corresponding to a SuperSpeed device having one alt-interface.
typedef struct {
  // clang-format off
    usb_interface_descriptor_t  interface;
    usb_endpoint_descriptor_t   ep1;
    usb_ss_ep_comp_descriptor_t ss_companion1;
    usb_endpoint_descriptor_t   ep2;
    usb_ss_ep_comp_descriptor_t ss_companion2;
    usb_interface_descriptor_t  alt_interface;
  // clang-format on
} alt_ss_config;

// The binary form of a usb keyboard descriptor. Contains an interface, hid descriptor, and endpoint
// descriptor.
constexpr uint8_t descriptor_binary_array[] = {9, 4, 1,  0,   1, 3, 0, 0,   0, 9, 33, 16, 1,
                                               0, 1, 34, 106, 0, 7, 5, 130, 3, 8, 0,  48};

constexpr alt_hs_config kTestHSInterface = {
    .interface =
        {
            .b_length = sizeof(usb_interface_descriptor_t),
            .b_descriptor_type = USB_DT_INTERFACE,
            .b_interface_number = 0,
            .b_alternate_setting = 0,
            .b_num_endpoints = 2,
            .b_interface_class = 8,
            .b_interface_sub_class = 6,
            .b_interface_protocol = 80,
            .i_interface = 0,
        },
    .ep1 =
        {
            .b_length = sizeof(usb_endpoint_descriptor_t),
            .b_descriptor_type = USB_DT_ENDPOINT,
            .b_endpoint_address = 0x81,
            .bm_attributes = 2,
            .w_max_packet_size = 1024,
            .b_interval = 0,
        },
    .ep2 =
        {
            .b_length = sizeof(usb_endpoint_descriptor_t),
            .b_descriptor_type = USB_DT_ENDPOINT,
            .b_endpoint_address = 2,
            .bm_attributes = 2,
            .w_max_packet_size = 1024,
            .b_interval = 0,
        },
    .hid_descriptor =
        {
            .bLength = sizeof(usb_hid_descriptor_for_test_t),
            .bDescriptorType = USB_DT_HID,
            .bcdHID = 0,
            .bCountryCode = 0,
            .bNumDescriptors = 0,
        },
    .alt_interface =
        {
            .b_length = sizeof(usb_interface_descriptor_t),
            .b_descriptor_type = USB_DT_INTERFACE,
            .b_interface_number = 0,
            .b_alternate_setting = 1,
            .b_num_endpoints = 2,
            .b_interface_class = 8,
            .b_interface_sub_class = 6,
            .b_interface_protocol = 80,
            .i_interface = 0,
        },
};

// Taken from a real UMS-class device.
constexpr alt_ss_config kTestSSInterface = {
    .interface =
        {
            .b_length = sizeof(usb_interface_descriptor_t),
            .b_descriptor_type = USB_DT_INTERFACE,
            .b_interface_number = 0,
            .b_alternate_setting = 0,
            .b_num_endpoints = 2,
            .b_interface_class = 8,
            .b_interface_sub_class = 6,
            .b_interface_protocol = 80,
            .i_interface = 0,
        },
    .ep1 =
        {
            .b_length = sizeof(usb_endpoint_descriptor_t),
            .b_descriptor_type = USB_DT_ENDPOINT,
            .b_endpoint_address = 0x81,
            .bm_attributes = 2,
            .w_max_packet_size = 1024,
            .b_interval = 0,
        },
    .ss_companion1 =
        {
            .b_length = sizeof(usb_ss_ep_comp_descriptor_t),
            .b_descriptor_type = USB_DT_SS_EP_COMPANION,
            .b_max_burst = 3,
            .bm_attributes = 0,
            .w_bytes_per_interval = 0,
        },
    .ep2 =
        {
            .b_length = sizeof(usb_endpoint_descriptor_t),
            .b_descriptor_type = USB_DT_ENDPOINT,
            .b_endpoint_address = 2,
            .bm_attributes = 2,
            .w_max_packet_size = 1024,
            .b_interval = 0,
        },
    .ss_companion2 =
        {
            .b_length = sizeof(usb_ss_ep_comp_descriptor_t),
            .b_descriptor_type = USB_DT_SS_EP_COMPANION,
            .b_max_burst = 3,
            .bm_attributes = 0,
            .w_bytes_per_interval = 0,
        },
    .alt_interface =
        {
            .b_length = sizeof(usb_interface_descriptor_t),
            .b_descriptor_type = USB_DT_INTERFACE,
            .b_interface_number = 0,
            .b_alternate_setting = 1,
            .b_num_endpoints = 2,
            .b_interface_class = 8,
            .b_interface_sub_class = 6,
            .b_interface_protocol = 80,
            .i_interface = 0,
        },
};

static void EXPECT_INTERFACE_EQ(usb_interface_descriptor_t a, usb_interface_descriptor_t b) {
  EXPECT_EQ(a.b_length, b.b_length);
  EXPECT_EQ(a.b_descriptor_type, b.b_descriptor_type);
  EXPECT_EQ(a.b_interface_number, b.b_interface_number);
  EXPECT_EQ(a.b_alternate_setting, b.b_alternate_setting);
  EXPECT_EQ(a.b_num_endpoints, b.b_num_endpoints);
  EXPECT_EQ(a.b_interface_class, b.b_interface_class);
  EXPECT_EQ(a.b_interface_sub_class, b.b_interface_sub_class);
  EXPECT_EQ(a.b_interface_protocol, b.b_interface_protocol);
  EXPECT_EQ(a.i_interface, b.i_interface);
}

static void EXPECT_ENDPOINT_EQ(usb_endpoint_descriptor_t a, usb_endpoint_descriptor_t b) {
  EXPECT_EQ(a.b_length, b.b_length);
  EXPECT_EQ(a.b_descriptor_type, b.b_descriptor_type);
  EXPECT_EQ(a.b_endpoint_address, b.b_endpoint_address);
  EXPECT_EQ(a.bm_attributes, b.bm_attributes);
  EXPECT_EQ(a.w_max_packet_size, b.w_max_packet_size);
  EXPECT_EQ(a.b_interval, b.b_interval);
}

static void EXPECT_SS_EP_COMP_EQ(usb_ss_ep_comp_descriptor_t a, usb_ss_ep_comp_descriptor_t b) {
  EXPECT_EQ(a.b_length, b.b_length);
  EXPECT_EQ(a.b_descriptor_type, b.b_descriptor_type);
  EXPECT_EQ(a.b_max_burst, b.b_max_burst);
  EXPECT_EQ(a.bm_attributes, b.bm_attributes);
  EXPECT_EQ(a.w_bytes_per_interval, b.w_bytes_per_interval);
}

static void EXPECT_DESCRIPTOR_EQ(const usb_descriptor_header_t* a,
                                 const usb_descriptor_header_t* b) {
  EXPECT_EQ(a->b_descriptor_type, b->b_descriptor_type);
  EXPECT_EQ(a->b_length, b->b_length);
}

// HighSpeedWrapperTest tests an InterfaceList's ability to process interface descriptors
// corresponding to a HighSpeed device structure (i.e. no SS-COMPANION descriptors).
class HighSpeedWrapperTest : public zxtest::Test {
 protected:
  void SetUp() {
    usb_protocol_t proto{&ops_, this};
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
    usb_ = ddk::UsbProtocolClient(&proto);
  }

  static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    memcpy(out_descs_buffer, &kTestHSInterface, descs_size);
    *out_descs_actual = descs_size;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) { return sizeof(kTestHSInterface); }

  usb_protocol_ops_t ops_{};
  ddk::UsbProtocolClient usb_;
};

TEST_F(HighSpeedWrapperTest, TestInterfaceRangeIterationSkippingAlt) {
  // This tests that for(x : y) syntax produces the correct interface descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  int count = 0;
  const auto interface = ilist->begin();
  EXPECT_INTERFACE_EQ(kTestHSInterface.interface, *interface->descriptor());
  for (auto& interface : *ilist) {
    EXPECT_TRUE(count++ < 1);
    EXPECT_INTERFACE_EQ(kTestHSInterface.interface, *interface.descriptor());
  };
}

TEST_F(HighSpeedWrapperTest, TestInterfaceRangeIterationNotSkippingAlt) {
  // This tests that for(x : y) syntax produces the correct interface descriptors.
  const usb_interface_descriptor_t wants[2] = {
      kTestHSInterface.interface,
      kTestHSInterface.alt_interface,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

  unsigned int count = 0;

  for (auto& interface : *ilist) {
    EXPECT_TRUE(count < std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *interface.descriptor());
  }
}

TEST_F(HighSpeedWrapperTest, TestEndpointRangeIteration) {
  // This tests that for(x : y) syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    for (auto ep : interface.GetEndpointList()) {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], ep.descriptor);
      EXPECT_FALSE(ep.has_companion);
    }
  }
}

TEST_F(HighSpeedWrapperTest, TestInterfaceAccessOps) {
  // This tests the various Interface access ops of a InterfaceList::iterator.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  auto itr = ilist->begin();
  int count = 0;
  do {
    EXPECT_TRUE(count++ < 1);
    auto& want = kTestHSInterface.interface;

    // operator->()
    const usb_interface_descriptor_t* ptr = itr->descriptor();
    EXPECT_INTERFACE_EQ(want, *ptr);

    // operator*()
    ptr = (*itr).descriptor();
    EXPECT_INTERFACE_EQ(want, *ptr);

    // .get()
    ptr = itr.get()->descriptor();
    EXPECT_INTERFACE_EQ(want, *ptr);
  } while (++itr != ilist->end());
}

TEST_F(HighSpeedWrapperTest, TestEndpointAccessOps) {
  // This tests the various endpoint descriptor ops of an Interface::iterator.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().begin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      auto& want = wants[count++];

      // operator->()
      const usb_endpoint_descriptor_t* ptr = &ep_itr->descriptor;
      EXPECT_ENDPOINT_EQ(want, *ptr);

      // operator*()
      ptr = &((*ep_itr).descriptor);
      EXPECT_ENDPOINT_EQ(want, *ptr);

      // .endpoint()
      ptr = &ep_itr.endpoint()->descriptor;
      EXPECT_ENDPOINT_EQ(want, *ptr);
    } while (++ep_itr != interface.GetEndpointList().end());
  }
}

TEST_F(HighSpeedWrapperTest, TestInterfaceIterationSkippingAlt) {
  // This tests that the iterator syntax produces the correct interface descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  auto itr = ilist->begin();
  int count = 0;
  do {
    EXPECT_TRUE(count++ < 1);
    EXPECT_INTERFACE_EQ(kTestHSInterface.interface, *itr->descriptor());
  } while (++itr != ilist->end());
}

TEST_F(HighSpeedWrapperTest, TestInterfaceIterationNotSkippingAlt) {
  // This tests that the iterator syntax produces the correct interface descriptors.
  const usb_interface_descriptor_t wants[2] = {
      kTestHSInterface.interface,
      kTestHSInterface.alt_interface,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

  auto itr = ilist->begin();
  unsigned int count = 0;
  do {
    EXPECT_TRUE(count < std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *itr->descriptor());
  } while (++itr != ilist->end());
}

TEST_F(HighSpeedWrapperTest, TestEndpointIteration) {
  // This tests that the iterator syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().begin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], ep_itr->descriptor);
      EXPECT_FALSE(ep_itr->has_companion);
    } while (++ep_itr != interface.GetEndpointList().end());
  }
}

TEST_F(HighSpeedWrapperTest, TestInterfaceConstIterationSkippingAlt) {
  // This tests that the const_iterator syntax produces the correct interface descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  auto itr = ilist->cbegin();
  int count = 0;
  do {
    EXPECT_TRUE(count++ < 1);
    EXPECT_INTERFACE_EQ(kTestHSInterface.interface, *itr->descriptor());
  } while (++itr != ilist->cend());
}

TEST_F(HighSpeedWrapperTest, TestInterfaceConstIterationNotSkippingAlt) {
  // This tests that the const_iterator syntax produces the correct interface descriptors.
  const usb_interface_descriptor_t wants[2] = {
      kTestHSInterface.interface,
      kTestHSInterface.alt_interface,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

  auto itr = ilist->cbegin();
  unsigned int count = 0;
  do {
    EXPECT_TRUE(count < std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *itr->descriptor());
  } while (++itr != ilist->cend());
}

TEST_F(HighSpeedWrapperTest, TestEndpointConstIteration) {
  // This tests that the const_iterator syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], ep_itr->descriptor);
      EXPECT_FALSE(ep_itr->has_companion);
    } while (++ep_itr != interface.GetEndpointList().cend());
  }
}

TEST_F(HighSpeedWrapperTest, TestDescriptorRangeIterationSkippingAlt) {
  // This tests that for(x : y) syntax produces the correct descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  for (auto& interface : *ilist) {
    auto descriptor_list_itr = interface.GetDescriptorList().cbegin();
    EXPECT_DESCRIPTOR_EQ(reinterpret_cast<const usb_descriptor_header_t*>(&kTestHSInterface.ep1),
                         descriptor_list_itr.header());
    ++descriptor_list_itr;
    EXPECT_DESCRIPTOR_EQ(reinterpret_cast<const usb_descriptor_header_t*>(&kTestHSInterface.ep2),
                         descriptor_list_itr.header());
    ++descriptor_list_itr;
    EXPECT_DESCRIPTOR_EQ(
        reinterpret_cast<const usb_descriptor_header_t*>(&kTestHSInterface.hid_descriptor),
        descriptor_list_itr.header());
    ++descriptor_list_itr;
    EXPECT_EQ(descriptor_list_itr, interface.GetDescriptorList().cend());
  }
}

// SuperSpeedWrapperTest tests an InterfaceList's ability to process interface descriptors
// corresponding to a SuperSpeed device structure.
class SuperSpeedWrapperTest : public zxtest::Test {
 protected:
  void SetUp() {
    usb_protocol_t proto{&ops_, this};
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
    usb_ = ddk::UsbProtocolClient(&proto);
  }

  static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    memcpy(out_descs_buffer, &kTestSSInterface, descs_size);
    *out_descs_actual = descs_size;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) { return sizeof(kTestSSInterface); }

  usb_protocol_ops_t ops_{};
  ddk::UsbProtocolClient usb_;
};

TEST_F(SuperSpeedWrapperTest, TestEndpointRangeIteration) {
  // This tests that for(x : y) syntax produces the correct endpoint descriptors.
  const usb_iter_endpoint_descriptor_t wants[2] = {
      {kTestSSInterface.ep1, kTestSSInterface.ss_companion1, true},
      {kTestSSInterface.ep2, kTestSSInterface.ss_companion2, true},
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep = interface.GetEndpointList().cbegin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count].descriptor, ep->descriptor);
      EXPECT_SS_EP_COMP_EQ(wants[count++].ss_companion, ep->ss_companion);
      EXPECT_TRUE(ep->has_companion);
    } while (++ep != interface.GetEndpointList().cend());
  }
}

TEST_F(SuperSpeedWrapperTest, TestEndpointIteration) {
  // This tests that the iterator syntax produces the correct endpoint descriptors.
  const usb_iter_endpoint_descriptor_t wants[2] = {
      {kTestSSInterface.ep1, kTestSSInterface.ss_companion1, true},
      {kTestSSInterface.ep2, kTestSSInterface.ss_companion2, true},
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count].descriptor, ep_itr->descriptor);
      EXPECT_SS_EP_COMP_EQ(wants[count++].ss_companion, ep_itr->ss_companion);
      EXPECT_TRUE(ep_itr->has_companion);
    } while (++ep_itr != interface.GetEndpointList().cend());
  }
}

TEST_F(SuperSpeedWrapperTest, TestEndpointConstIteration) {
  // This tests that the const_iterator syntax produces the correct endpoint descriptors.
  const usb_iter_endpoint_descriptor_t wants[2] = {
      {kTestSSInterface.ep1, kTestSSInterface.ss_companion1, true},
      {kTestSSInterface.ep2, kTestSSInterface.ss_companion2, true},
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  unsigned int count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      EXPECT_TRUE(count < std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count].descriptor, ep_itr->descriptor);
      EXPECT_SS_EP_COMP_EQ(wants[count++].ss_companion, ep_itr->ss_companion);
      EXPECT_TRUE(ep_itr->has_companion);
    } while (++ep_itr != interface.GetEndpointList().end());
  }
}

// HighSpeedWrapperTest tests an InterfaceList's ability to process interface descriptors
// corresponding to a HighSpeed device structure (i.e. no SS-COMPANION descriptors).
class BinaryHidDescriptorTest : public zxtest::Test {
 protected:
  void SetUp() {
    usb_protocol_t proto{&ops_, this};
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
    usb_ = ddk::UsbProtocolClient(&proto);
  }

  static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    memcpy(out_descs_buffer, &descriptor_binary_array, descs_size);
    *out_descs_actual = descs_size;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) { return sizeof(descriptor_binary_array); }

  usb_protocol_ops_t ops_{};
  ddk::UsbProtocolClient usb_;
};

TEST_F(BinaryHidDescriptorTest, TestBinaryHidDescriptor) {
  // This tests that for(x : y) syntax produces the correct descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  usb_hid_descriptor_t* hid_desc = nullptr;
  usb_endpoint_descriptor_t* endpt = nullptr;
  int iface_count = 0;
  for (auto interface : *ilist) {
    if (iface_count) {
      break;
    }
    for (auto& descriptor : interface.GetDescriptorList()) {
      if (descriptor.b_descriptor_type == USB_DT_HID) {
        hid_desc = (usb_hid_descriptor_t*)&descriptor;
        if (endpt) {
          break;
        }
      } else if (descriptor.b_descriptor_type == USB_DT_ENDPOINT) {
        if (usb_ep_direction((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_IN &&
            usb_ep_type((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_INTERRUPT) {
          endpt = (usb_endpoint_descriptor_t*)&descriptor;
          if (hid_desc) {
            break;
          }
        }
      }
    }
  }
  ASSERT_TRUE(hid_desc);
  ASSERT_TRUE(endpt);
}

}  // namespace usb

int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
