#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace {

constexpr usb_descriptor_header_t kTestDescriptorHeader = {
    .bLength = sizeof(usb_descriptor_header_t),
    .bDescriptorType = 0,
};

constexpr usb_interface_descriptor_t kTestUsbInterfaceDescriptor = {
    .bLength = sizeof(usb_interface_descriptor_t),
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = 8,
    .bInterfaceSubClass = 6,
    .bInterfaceProtocol = 80,
    .iInterface = 0,
};

class UsbLibTest : public zxtest::Test {
 public:
  void SetUp() override {
    proto_.ops = &ops_;
    proto_.ctx = this;
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
  }

 protected:
  static void UsbGetDescriptors(void* ctx, void* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    auto test = reinterpret_cast<UsbLibTest*>(ctx);
    size_t descriptors_length = test->GetDescriptorLength();
    if (descs_size < descriptors_length) {
      descriptors_length = descs_size;
    }
    memcpy(out_descs_buffer, test->GetDescriptors(), descriptors_length);
    *out_descs_actual = descriptors_length;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) {
    auto test = reinterpret_cast<UsbLibTest*>(ctx);
    return test->GetDescriptorLength();
  }

  void SetDescriptors(void* descriptors) { descriptors_ = descriptors; }

  void* GetDescriptors() { return descriptors_; }

  void SetDescriptorLength(size_t descriptor_length) { descriptor_length_ = descriptor_length; }

  size_t GetDescriptorLength() { return descriptor_length_; }

  usb_protocol_t* GetUsbProto() { return &proto_; }

  usb_protocol_t proto_{};
  usb_protocol_ops_t ops_{};
  void* descriptors_ = nullptr;
  size_t descriptor_length_ = 0;
};

TEST_F(UsbLibTest, TestUsbDescIterNextNormal) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto desc = usb_desc_iter_next(&iter);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_EQ(nullptr, usb_desc_iter_next(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterNextOverflow) {
  usb_desc_iter_t iter;
  usb_descriptor_header_t desc = kTestDescriptorHeader;
  // Length is invalid and longer than the actual length.
  desc.bLength++;
  SetDescriptors((void*)&desc);
  SetDescriptorLength(sizeof(desc));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_next(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterNextHeaderTooShort) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader) - 1);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_next(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterPeekNormal) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto desc = usb_desc_iter_peek(&iter);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_EQ(desc, usb_desc_iter_next(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescPeekOverflow) {
  usb_desc_iter_t iter;
  usb_descriptor_header_t desc = kTestDescriptorHeader;
  // Length is invalid and longer than the actual length.
  desc.bLength++;
  SetDescriptors((void*)&desc);
  SetDescriptorLength(sizeof(desc));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterPeekHeaderTooShort) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader) - 1);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescClone) {
  usb_desc_iter_t src;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  auto status = usb_desc_iter_init(GetUsbProto(), &src);
  ASSERT_OK(status);
  usb_desc_iter_t dest;
  ASSERT_OK(usb_desc_iter_clone(&src, &dest));
  // This should not affect dest.
  usb_desc_iter_release(&src);
  auto desc = usb_desc_iter_next(&dest);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_EQ(nullptr, usb_desc_iter_next(&dest));
  usb_desc_iter_release(&dest);
}

TEST_F(UsbLibTest, TestUsbDescAdvanceReset) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_TRUE(usb_desc_iter_advance(&iter));
  ASSERT_FALSE(usb_desc_iter_advance(&iter));
  usb_desc_iter_reset(&iter);
  auto desc = usb_desc_iter_next(&iter);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_EQ(nullptr, usb_desc_iter_next(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescGetStructureNormal) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestUsbInterfaceDescriptor);
  SetDescriptorLength(sizeof(kTestUsbInterfaceDescriptor));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto desc = usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor));
  ASSERT_EQ(memcmp(desc, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor)), 0);
  ASSERT_TRUE(usb_desc_iter_advance(&iter));
  ASSERT_EQ(nullptr, usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor)));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescGetStructureOverflow) {
  usb_desc_iter_t iter;
  usb_interface_descriptor_t desc = kTestUsbInterfaceDescriptor;
  SetDescriptors((void*)&desc);
  SetDescriptorLength(sizeof(desc) - 1);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor)));
  usb_desc_iter_release(&iter);
}

}  // namespace
