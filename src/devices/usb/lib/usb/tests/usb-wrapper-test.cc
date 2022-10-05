// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/hidbus/c/banjo.h>
#include <fuchsia/hardware/usb/descriptor/c/banjo.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/hid.h>

#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb {

struct usb_hid_descriptor_for_test_t {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdHID;
  uint8_t bCountryCode;
  uint8_t bNumDescriptors;
} __attribute__((packed));

// The interface configuration corresponding to a HighSpeed device having one alt-interface.
struct alt_hs_config {
  usb_interface_descriptor_t interface;
  usb_endpoint_descriptor_t ep1;
  usb_endpoint_descriptor_t ep2;
  usb_hid_descriptor_for_test_t hid_descriptor;
  usb_interface_descriptor_t alt_interface;
};

// The interface configuration corresponding to a SuperSpeed device having one alt-interface.
struct alt_ss_config {
  usb_interface_descriptor_t interface;
  usb_endpoint_descriptor_t ep1;
  usb_ss_ep_comp_descriptor_t ss_companion1;
  usb_endpoint_descriptor_t ep2;
  usb_ss_ep_comp_descriptor_t ss_companion2;
  usb_interface_descriptor_t alt_interface;
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

static void EXPECT_ENDPOINT_EQ(const usb_endpoint_descriptor_t a,
                               const usb_endpoint_descriptor_t b) {
  EXPECT_EQ(a.b_length, b.b_length);
  EXPECT_EQ(a.b_descriptor_type, b.b_descriptor_type);
  EXPECT_EQ(a.b_endpoint_address, b.b_endpoint_address);
  EXPECT_EQ(a.bm_attributes, b.bm_attributes);
  EXPECT_EQ(a.w_max_packet_size, b.w_max_packet_size);
  EXPECT_EQ(a.b_interval, b.b_interval);
}

static void EXPECT_SS_EP_COMP_EQ(const usb_ss_ep_comp_descriptor_t a,
                                 const usb_ss_ep_comp_descriptor_t b) {
  EXPECT_EQ(a.b_length, b.b_length);
  EXPECT_EQ(a.b_descriptor_type, b.b_descriptor_type);
  EXPECT_EQ(a.b_max_burst, b.b_max_burst);
  EXPECT_EQ(a.bm_attributes, b.bm_attributes);
  EXPECT_EQ(a.w_bytes_per_interval, b.w_bytes_per_interval);
}

static void EXPECT_DESCRIPTOR_EQ(const usb_descriptor_header_t* a,
                                 const usb_descriptor_header_t* b) {
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  EXPECT_EQ(a->b_descriptor_type, b->b_descriptor_type);
  EXPECT_EQ(a->b_length, b->b_length);
}

// WrapperTest is templated on the descriptor data used for the test, WrapperTest<&kDescriptors>
// will set up a test fixture with a USB protocol client that implements GetDescriptors and
// GetDescriptorsLength for the provided kDescriptors.
template <auto* descriptors>
class WrapperTest : public zxtest::Test {
 protected:
  void SetUp() {
    usb_protocol_t proto{&ops_, this};
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
    usb_ = ddk::UsbProtocolClient(&proto);
  }

  static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    memcpy(out_descs_buffer, descriptors, descs_size);
    *out_descs_actual = descs_size;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) { return sizeof(*descriptors); }

  usb_protocol_ops_t ops_{};
  ddk::UsbProtocolClient usb_;
};

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

// HighSpeedWrapperTest tests an InterfaceList's ability to process interface descriptors
// corresponding to a HighSpeed device structure (i.e. no SS-COMPANION descriptors).
using HighSpeedWrapperTest = WrapperTest<&kTestHSInterface>;

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
  EXPECT_EQ(count, 1);
}

TEST_F(HighSpeedWrapperTest, TestInterfaceRangeIterationNotSkippingAlt) {
  // This tests that for(x : y) syntax produces the correct interface descriptors.
  const usb_interface_descriptor_t wants[2] = {
      kTestHSInterface.interface,
      kTestHSInterface.alt_interface,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, false, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    ASSERT_LT(count, std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *interface.descriptor());
  }
  EXPECT_EQ(count, std::size(wants));
}

TEST_F(HighSpeedWrapperTest, TestEndpointRangeIteration) {
  // This tests that for(x : y) syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    for (auto ep : interface.GetEndpointList()) {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], *ep.descriptor());
      EXPECT_FALSE(ep.has_companion());
    }
  }
  EXPECT_EQ(count, std::size(wants));
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
  EXPECT_EQ(count, 1);
}

TEST_F(HighSpeedWrapperTest, TestEndpointAccessOps) {
  // This tests the various endpoint descriptor ops of an Interface::iterator.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().begin();
    do {
      ASSERT_LT(count, std::size(wants));
      auto& want = wants[count++];

      // operator->()
      EXPECT_ENDPOINT_EQ(want, *ep_itr->descriptor());

      // operator*()
      EXPECT_ENDPOINT_EQ(want, *(*ep_itr).descriptor());
    } while (++ep_itr != interface.GetEndpointList().end());
  }
  EXPECT_EQ(count, std::size(wants));
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
  EXPECT_EQ(count, 1);
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
  size_t count = 0;
  do {
    ASSERT_LT(count, std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *itr->descriptor());
  } while (++itr != ilist->end());
  EXPECT_EQ(count, std::size(wants));
}

TEST_F(HighSpeedWrapperTest, TestEndpointIteration) {
  // This tests that the iterator syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().begin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], *ep_itr->descriptor());
      EXPECT_FALSE(ep_itr->has_companion());
    } while (++ep_itr != interface.GetEndpointList().end());
  }
  EXPECT_EQ(count, std::size(wants));
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
  EXPECT_EQ(count, 1);
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
  size_t count = 0;
  do {
    ASSERT_LT(count, std::size(wants));
    EXPECT_INTERFACE_EQ(wants[count++], *itr->descriptor());
  } while (++itr != ilist->cend());
  EXPECT_EQ(count, std::size(wants));
}

TEST_F(HighSpeedWrapperTest, TestEndpointConstIteration) {
  // This tests that the const_iterator syntax produces the correct endpoint descriptors.
  const usb_endpoint_descriptor_t wants[2] = {
      kTestHSInterface.ep1,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], *ep_itr->descriptor());
      EXPECT_FALSE(ep_itr->has_companion());
    } while (++ep_itr != interface.GetEndpointList().cend());
  }
  EXPECT_EQ(count, std::size(wants));
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
using SuperSpeedWrapperTest = WrapperTest<&kTestSSInterface>;

TEST_F(SuperSpeedWrapperTest, TestEndpointRangeIteration) {
  // This tests that for(x : y) syntax produces the correct endpoint descriptors.
  const Endpoint wants[2] = {
      Endpoint(&kTestSSInterface.ep1, &kTestSSInterface.ss_companion1),
      Endpoint(&kTestSSInterface.ep2, &kTestSSInterface.ss_companion2),
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep = interface.GetEndpointList().cbegin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(*wants[count].descriptor(), *ep->descriptor());
      ASSERT_TRUE(ep->has_companion());
      EXPECT_SS_EP_COMP_EQ(*wants[count++].ss_companion().value(), *ep->ss_companion().value());
    } while (++ep != interface.GetEndpointList().cend());
  }
  EXPECT_EQ(count, std::size(wants));
}

TEST_F(SuperSpeedWrapperTest, TestEndpointIteration) {
  // This tests that the iterator syntax produces the correct endpoint descriptors.
  const Endpoint wants[2] = {
      Endpoint(&kTestSSInterface.ep1, &kTestSSInterface.ss_companion1),
      Endpoint(&kTestSSInterface.ep2, &kTestSSInterface.ss_companion2),
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(*wants[count].descriptor(), *ep_itr->descriptor());
      ASSERT_TRUE(ep_itr->has_companion());
      EXPECT_SS_EP_COMP_EQ(*wants[count++].ss_companion().value(), *ep_itr->ss_companion().value());
    } while (++ep_itr != interface.GetEndpointList().cend());
  }
  EXPECT_EQ(count, std::size(wants));
}

TEST_F(SuperSpeedWrapperTest, TestEndpointConstIteration) {
  // This tests that the const_iterator syntax produces the correct endpoint descriptors.
  const Endpoint wants[2] = {
      Endpoint(&kTestSSInterface.ep1, &kTestSSInterface.ss_companion1),
      Endpoint(&kTestSSInterface.ep2, &kTestSSInterface.ss_companion2),
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().cbegin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(*wants[count].descriptor(), *ep_itr->descriptor());
      ASSERT_TRUE(ep_itr->has_companion());
      EXPECT_SS_EP_COMP_EQ(*wants[count++].ss_companion().value(), *ep_itr->ss_companion().value());
    } while (++ep_itr != interface.GetEndpointList().end());
  }
  EXPECT_EQ(count, std::size(wants));
}

constexpr usb_endpoint_descriptor_t kInvalidEnpdoint = {
    .b_length = sizeof(usb_endpoint_descriptor_t),
    .b_descriptor_type = USB_DT_ENDPOINT,
};

constexpr alt_hs_config kTestInvalidInterface = []() -> alt_hs_config {
  auto config = kTestHSInterface;
  config.ep1 = kInvalidEnpdoint;
  return config;
}();

// InvalidDataWrapperTest tests that the iterator syntax produces the correct endpoint descriptors
// despite invalid data, i.e. a zeroed out endpoint. This tests for a regression where iterator
// equality assumed that endpoint addresses were unique and nonzero, which may not be the case for
// test data.
using InvalidDataWrapperTest = WrapperTest<&kTestInvalidInterface>;

TEST_F(InvalidDataWrapperTest, TestEndpointIterationInvalidData) {
  const usb_endpoint_descriptor_t wants[2] = {
      kInvalidEnpdoint,
      kTestHSInterface.ep2,
  };

  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  size_t count = 0;
  for (auto& interface : *ilist) {
    auto ep_itr = interface.GetEndpointList().begin();
    do {
      ASSERT_LT(count, std::size(wants));
      EXPECT_ENDPOINT_EQ(wants[count++], *ep_itr->descriptor());
      EXPECT_FALSE(ep_itr->has_companion());
    } while (++ep_itr != interface.GetEndpointList().end());
  }
  EXPECT_EQ(count, std::size(wants));
}

constexpr uint8_t kBinaryArrayDescriptor[] = {9, 4, 1,  0,   1, 3, 0, 0,   0, 9, 33, 16, 1,
                                              0, 1, 34, 106, 0, 7, 5, 130, 3, 8, 0,  48};

// BinaryHidDescriptorTest tests an InterfaceList's ability to process interface descriptors
// created from binary data. In this case, a USB keyboard descriptor containing an interface, HID
// descriptor, and endpoint.
using BinaryHidDescriptorTest = WrapperTest<&kBinaryArrayDescriptor>;

TEST_F(BinaryHidDescriptorTest, TestBinaryHidDescriptor) {
  // This tests that for(x : y) syntax produces the correct descriptors.
  std::optional<InterfaceList> ilist;
  ASSERT_OK(InterfaceList::Create(usb_, true, &ilist));

  const usb_hid_descriptor_t* hid_desc = nullptr;
  const usb_endpoint_descriptor_t* endpoint_desc = nullptr;
  size_t count = 0;
  for (auto interface : *ilist) {
    ASSERT_LT(count, 1);
    count++;

    for (auto& descriptor : interface.GetDescriptorList()) {
      EXPECT_TRUE(descriptor.b_descriptor_type == USB_DT_HID ||
                  descriptor.b_descriptor_type == USB_DT_ENDPOINT);

      if (descriptor.b_descriptor_type == USB_DT_HID) {
        hid_desc = reinterpret_cast<const usb_hid_descriptor_t*>(&descriptor);
      } else if (descriptor.b_descriptor_type == USB_DT_ENDPOINT) {
        endpoint_desc = reinterpret_cast<const usb_endpoint_descriptor_t*>(&descriptor);
        EXPECT_EQ(usb_ep_direction(endpoint_desc), USB_ENDPOINT_IN);
        EXPECT_EQ(usb_ep_type(endpoint_desc), USB_ENDPOINT_INTERRUPT);
      }
    }
  }
  EXPECT_EQ(count, 1);
  EXPECT_NOT_NULL(hid_desc);
  EXPECT_NOT_NULL(endpoint_desc);
}

}  // namespace usb

int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
