// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/pci.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_pci_get_nth_device tests.

std::unique_ptr<SystemCallTest> ZxPciGetNthDevice(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, uint32_t index,
                                                  zx_pcie_device_info_t* out_info,
                                                  zx_handle_t* out_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_get_nth_device", result, result_name);
  value->AddInput(handle);
  value->AddInput(index);
  value->AddInput(reinterpret_cast<uint64_t>(out_info));
  value->AddInput(reinterpret_cast<uint64_t>(out_handle));
  return value;
}

#define PCI_GET_NTH_DEVICE_DISPLAY_TEST_CONTENT(result, expected)                               \
  zx_pcie_device_info_t out_info = {.vendor_id = 1,                                             \
                                    .device_id = 2,                                             \
                                    .base_class = 3,                                            \
                                    .sub_class = 4,                                             \
                                    .program_interface = 5,                                     \
                                    .revision_id = 6,                                           \
                                    .bus_id = 7,                                                \
                                    .dev_id = 8,                                                \
                                    .func_id = 9};                                              \
  zx_handle_t out_handle = kHandleOut;                                                          \
  PerformDisplayTest("$plt(zx_pci_get_nth_device)",                                             \
                     ZxPciGetNthDevice(result, #result, kHandle, 1234, &out_info, &out_handle), \
                     expected);

#define PCI_GET_NTH_DEVICE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    PCI_GET_NTH_DEVICE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    PCI_GET_NTH_DEVICE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_GET_NTH_DEVICE_DISPLAY_TEST(
    ZxPciGetNthDevice, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_get_nth_device("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "index: \x1B[32muint32\x1B[0m = \x1B[34m1234\x1B[0m)\n"
    "  out_info: \x1B[32mzx_pcie_device_info_t\x1B[0m = {\n"
    "    vendor_id: \x1B[32muint16\x1B[0m = \x1B[34m1\x1B[0m\n"
    "    device_id: \x1B[32muint16\x1B[0m = \x1B[34m2\x1B[0m\n"
    "    base_class: \x1B[32muint8\x1B[0m = \x1B[34m3\x1B[0m\n"
    "    sub_class: \x1B[32muint8\x1B[0m = \x1B[34m4\x1B[0m\n"
    "    program_interface: \x1B[32muint8\x1B[0m = \x1B[34m5\x1B[0m\n"
    "    revision_id: \x1B[32muint8\x1B[0m = \x1B[34m6\x1B[0m\n"
    "    bus_id: \x1B[32muint8\x1B[0m = \x1B[34m7\x1B[0m\n"
    "    dev_id: \x1B[32muint8\x1B[0m = \x1B[34m8\x1B[0m\n"
    "    func_id: \x1B[32muint8\x1B[0m = \x1B[34m9\x1B[0m\n"
    "  }\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

// zx_pci_enable_bus_master tests.

std::unique_ptr<SystemCallTest> ZxPciEnableBusMaster(int64_t result, std::string_view result_name,
                                                     zx_handle_t handle, bool enable) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_enable_bus_master", result, result_name);
  value->AddInput(handle);
  value->AddInput(enable);
  return value;
}

#define PCI_ENABLE_BUS_MASTER_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_pci_enable_bus_master)",               \
                     ZxPciEnableBusMaster(result, #result, kHandle, true), expected);

#define PCI_ENABLE_BUS_MASTER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    PCI_ENABLE_BUS_MASTER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    PCI_ENABLE_BUS_MASTER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_ENABLE_BUS_MASTER_DISPLAY_TEST(ZxPciEnableBusMaster, ZX_OK,
                                   "\n"
                                   "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                   "zx_pci_enable_bus_master("
                                   "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                                   "enable: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m)\n"
                                   "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pci_reset_device tests.

std::unique_ptr<SystemCallTest> ZxPciResetDevice(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_reset_device", result, result_name);
  value->AddInput(handle);
  return value;
}

#define PCI_RESET_DEVICE_DISPLAY_TEST_CONTENT(result, expected)                               \
  PerformDisplayTest("$plt(zx_pci_reset_device)", ZxPciResetDevice(result, #result, kHandle), \
                     expected);

#define PCI_RESET_DEVICE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PCI_RESET_DEVICE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PCI_RESET_DEVICE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_RESET_DEVICE_DISPLAY_TEST(
    ZxPciResetDevice, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_reset_device(handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pci_config_read tests.

std::unique_ptr<SystemCallTest> ZxPciConfigRead(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint16_t offset, size_t width,
                                                uint32_t* out_val) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_config_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(offset);
  value->AddInput(width);
  value->AddInput(reinterpret_cast<uint64_t>(out_val));
  return value;
}

#define PCI_CONFIG_READ_DISPLAY_TEST_CONTENT(result, expected) \
  uint32_t out_val = 1234;                                     \
  PerformDisplayTest("$plt(zx_pci_config_read)",               \
                     ZxPciConfigRead(result, #result, kHandle, 1000, 4, &out_val), expected);

#define PCI_CONFIG_READ_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    PCI_CONFIG_READ_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    PCI_CONFIG_READ_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_CONFIG_READ_DISPLAY_TEST(
    ZxPciConfigRead, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_config_read("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "offset: \x1B[32muint16\x1B[0m = \x1B[34m1000\x1B[0m, "
    "width: \x1B[32msize\x1B[0m = \x1B[34m4\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_val: \x1B[32muint32\x1B[0m = \x1B[34m1234\x1B[0m)\n");

// zx_pci_config_write tests.

std::unique_ptr<SystemCallTest> ZxPciConfigWrite(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint16_t offset, size_t width,
                                                 uint32_t val) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_config_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(offset);
  value->AddInput(width);
  value->AddInput(val);
  return value;
}

#define PCI_CONFIG_WRITE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_pci_config_write)",               \
                     ZxPciConfigWrite(result, #result, kHandle, 1000, 4, 1234), expected);

#define PCI_CONFIG_WRITE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PCI_CONFIG_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PCI_CONFIG_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_CONFIG_WRITE_DISPLAY_TEST(ZxPciConfigWrite, ZX_OK,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_pci_config_write("
                              "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                              "offset: \x1B[32muint16\x1B[0m = \x1B[34m1000\x1B[0m, "
                              "width: \x1B[32msize\x1B[0m = \x1B[34m4\x1B[0m, "
                              "val: \x1B[32muint32\x1B[0m = \x1B[34m1234\x1B[0m)\n"
                              "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pci_cfg_pio_rw tests.

std::unique_ptr<SystemCallTest> ZxPciCfgPioRw(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, uint8_t bus, uint8_t dev,
                                              uint8_t func, uint8_t offset, uint32_t* val,
                                              size_t width, bool write) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_cfg_pio_rw", result, result_name);
  value->AddInput(handle);
  value->AddInput(bus);
  value->AddInput(dev);
  value->AddInput(func);
  value->AddInput(offset);
  value->AddInput(reinterpret_cast<uint64_t>(val));
  value->AddInput(width);
  value->AddInput(write);
  return value;
}

#define PCI_CFG_PIO_RW_DISPLAY_TEST_CONTENT(result, write, expected)                        \
  uint32_t val = 1234;                                                                      \
  PerformDisplayTest("$plt(zx_pci_cfg_pio_rw)",                                             \
                     ZxPciCfgPioRw(result, #result, kHandle, 1, 2, 3, 100, &val, 4, write), \
                     expected);

#define PCI_CFG_PIO_RW_DISPLAY_TEST(name, errno, write, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    PCI_CFG_PIO_RW_DISPLAY_TEST_CONTENT(errno, write, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    PCI_CFG_PIO_RW_DISPLAY_TEST_CONTENT(errno, write, expected);  \
  }

PCI_CFG_PIO_RW_DISPLAY_TEST(
    ZxPciCfgPioRwRead, ZX_OK, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_cfg_pio_rw("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "bus: \x1B[32muint8\x1B[0m = \x1B[34m1\x1B[0m, "
    "dev: \x1B[32muint8\x1B[0m = \x1B[34m2\x1B[0m, "
    "func: \x1B[32muint8\x1B[0m = \x1B[34m3\x1B[0m, "
    "offset: \x1B[32muint8\x1B[0m = \x1B[34m100\x1B[0m, "
    "width: \x1B[32msize\x1B[0m = \x1B[34m4\x1B[0m, "
    "write: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (val: \x1B[32muint32\x1B[0m = \x1B[34m1234\x1B[0m)\n");

PCI_CFG_PIO_RW_DISPLAY_TEST(ZxPciCfgPioRwWrite, ZX_OK, true,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_pci_cfg_pio_rw("
                            "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                            "bus: \x1B[32muint8\x1B[0m = \x1B[34m1\x1B[0m, "
                            "dev: \x1B[32muint8\x1B[0m = \x1B[34m2\x1B[0m, "
                            "func: \x1B[32muint8\x1B[0m = \x1B[34m3\x1B[0m, "
                            "offset: \x1B[32muint8\x1B[0m = \x1B[34m100\x1B[0m, "
                            "width: \x1B[32msize\x1B[0m = \x1B[34m4\x1B[0m, "
                            "val: \x1B[32muint32\x1B[0m = \x1B[34m1234\x1B[0m, "
                            "write: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m)\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pci_get_bar tests.

std::unique_ptr<SystemCallTest> ZxPciGetBar(int64_t result, std::string_view result_name,
                                            zx_handle_t handle, uint32_t bar_num,
                                            zx_pci_bar_t* out_bar, zx_handle_t* out_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_get_bar", result, result_name);
  value->AddInput(handle);
  value->AddInput(bar_num);
  value->AddInput(reinterpret_cast<uint64_t>(out_bar));
  value->AddInput(reinterpret_cast<uint64_t>(out_handle));
  return value;
}

#define PCI_GET_BAR_UNUSED_DISPLAY_TEST_CONTENT(result, expected)      \
  zx_pci_bar_t out_bar = {.id = 1000, .type = ZX_PCI_BAR_TYPE_UNUSED}; \
  zx_handle_t out_handle = kHandleOut;                                 \
  PerformDisplayTest("$plt(zx_pci_get_bar)",                           \
                     ZxPciGetBar(result, #result, kHandle, 1, &out_bar, &out_handle), expected);

#define PCI_GET_BAR_UNUSED_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    PCI_GET_BAR_UNUSED_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    PCI_GET_BAR_UNUSED_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_GET_BAR_UNUSED_DISPLAY_TEST(
    ZxPciGetBarUnused, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_get_bar("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "bar_num: \x1B[32muint32\x1B[0m = \x1B[34m1\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n"
    "    out_bar: \x1B[32mzx_pci_bar_t\x1B[0m = { "
    "id: \x1B[32muint32\x1B[0m = \x1B[34m1000\x1B[0m, "
    "type: \x1B[32mzx.pci_bar_type\x1B[0m = \x1B[34mZX_PCI_BAR_TYPE_UNUSED\x1B[0m"
    " }\n");

#define PCI_GET_BAR_MMIO_DISPLAY_TEST_CONTENT(result, expected)                          \
  zx_pci_bar_t out_bar = {.id = 1000, .type = ZX_PCI_BAR_TYPE_MMIO, .handle = kHandle2}; \
  zx_handle_t out_handle = kHandleOut;                                                   \
  PerformDisplayTest("$plt(zx_pci_get_bar)",                                             \
                     ZxPciGetBar(result, #result, kHandle, 2, &out_bar, &out_handle), expected);

#define PCI_GET_BAR_MMIO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PCI_GET_BAR_MMIO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PCI_GET_BAR_MMIO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_GET_BAR_MMIO_DISPLAY_TEST(
    ZxPciGetBarMmio, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_get_bar("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "bar_num: \x1B[32muint32\x1B[0m = \x1B[34m2\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n"
    "    out_bar: \x1B[32mzx_pci_bar_t\x1B[0m = { "
    "id: \x1B[32muint32\x1B[0m = \x1B[34m1000\x1B[0m, "
    "type: \x1B[32mzx.pci_bar_type\x1B[0m = \x1B[34mZX_PCI_BAR_TYPE_MMIO\x1B[0m, "
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1222\x1B[0m"
    " }\n");

#define PCI_GET_BAR_PIO_DISPLAY_TEST_CONTENT(result, expected)                                     \
  zx_pci_bar_t out_bar = {.id = 1000, .type = ZX_PCI_BAR_TYPE_PIO, .size = 1024, .addr = 0x45678}; \
  zx_handle_t out_handle = kHandleOut;                                                             \
  PerformDisplayTest("$plt(zx_pci_get_bar)",                                                       \
                     ZxPciGetBar(result, #result, kHandle, 3, &out_bar, &out_handle), expected);

#define PCI_GET_BAR_PIO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    PCI_GET_BAR_PIO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    PCI_GET_BAR_PIO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_GET_BAR_PIO_DISPLAY_TEST(
    ZxPciGetBarPio, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_get_bar("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "bar_num: \x1B[32muint32\x1B[0m = \x1B[34m3\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n"
    "    out_bar: \x1B[32mzx_pci_bar_t\x1B[0m = {\n"
    "      id: \x1B[32muint32\x1B[0m = \x1B[34m1000\x1B[0m\n"
    "      type: \x1B[32mzx.pci_bar_type\x1B[0m = \x1B[34mZX_PCI_BAR_TYPE_PIO\x1B[0m\n"
    "      size: \x1B[32msize\x1B[0m = \x1B[34m1024\x1B[0m\n"
    "      addr: \x1B[32muintptr\x1B[0m = \x1B[34m0000000000045678\x1B[0m\n"
    "    }\n");

// zx_pci_map_interrupt tests.

std::unique_ptr<SystemCallTest> ZxPciMapInterrupt(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, int32_t which_irq,
                                                  zx_handle_t* out_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_map_interrupt", result, result_name);
  value->AddInput(handle);
  value->AddInput(which_irq);
  value->AddInput(reinterpret_cast<uint64_t>(out_handle));
  return value;
}

#define PCI_MAP_INTERRUPT_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out_handle = kHandleOut;                           \
  PerformDisplayTest("$plt(zx_pci_map_interrupt)",               \
                     ZxPciMapInterrupt(result, #result, kHandle, 5, &out_handle), expected);

#define PCI_MAP_INTERRUPT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    PCI_MAP_INTERRUPT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    PCI_MAP_INTERRUPT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_MAP_INTERRUPT_DISPLAY_TEST(
    ZxPciMapInterrupt, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_map_interrupt("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "which_irq: \x1B[32mint32\x1B[0m = \x1B[34m5\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

// zx_pci_query_irq_mode tests.

std::unique_ptr<SystemCallTest> ZxPciQueryIrqMode(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, uint32_t mode,
                                                  uint32_t* out_max_irqs) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_query_irq_mode", result, result_name);
  value->AddInput(handle);
  value->AddInput(mode);
  value->AddInput(reinterpret_cast<uint64_t>(out_max_irqs));
  return value;
}

#define PCI_QUERY_IRQ_MODE_DISPLAY_TEST_CONTENT(result, expected) \
  uint32_t out_max_irqs = 12;                                     \
  PerformDisplayTest("$plt(zx_pci_query_irq_mode)",               \
                     ZxPciQueryIrqMode(result, #result, kHandle, 0, &out_max_irqs), expected);

#define PCI_QUERY_IRQ_MODE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    PCI_QUERY_IRQ_MODE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    PCI_QUERY_IRQ_MODE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_QUERY_IRQ_MODE_DISPLAY_TEST(
    ZxPciQueryIrqMode, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pci_query_irq_mode("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "mode: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_max_irqs: \x1B[32muint32\x1B[0m = \x1B[34m12\x1B[0m)\n");

// zx_pci_set_irq_mode tests.

std::unique_ptr<SystemCallTest> ZxPciSetIrqMode(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t mode,
                                                uint32_t requested_irq_count) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_set_irq_mode", result, result_name);
  value->AddInput(handle);
  value->AddInput(mode);
  value->AddInput(requested_irq_count);
  return value;
}

#define PCI_SET_IRQ_MODE_DISPLAY_TEST_CONTENT(result, expected)                                    \
  PerformDisplayTest("$plt(zx_pci_set_irq_mode)", ZxPciSetIrqMode(result, #result, kHandle, 0, 5), \
                     expected);

#define PCI_SET_IRQ_MODE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PCI_SET_IRQ_MODE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PCI_SET_IRQ_MODE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_SET_IRQ_MODE_DISPLAY_TEST(ZxPciSetIrqMode, ZX_OK,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_pci_set_irq_mode("
                              "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                              "mode: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m, "
                              "requested_irq_count: \x1B[32muint32\x1B[0m = \x1B[34m5\x1B[0m)\n"
                              "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pci_init tests.

std::unique_ptr<SystemCallTest> ZxPciInit(int64_t result, std::string_view result_name,
                                          zx_handle_t handle, const zx_pci_init_arg_t* init_buf,
                                          uint32_t len) {
  auto value = std::make_unique<SystemCallTest>("zx_pci_init", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(init_buf));
  value->AddInput(len);
  return value;
}

#define PCI_INIT_DISPLAY_TEST_CONTENT(result, expected)                                   \
  std::vector<uint8_t> buffer(sizeof(zx_pci_init_arg_t) +                                 \
                              3 * sizeof(zx_pci_init_arg_addr_window_t));                 \
  zx_pci_init_arg_t* init_buf = reinterpret_cast<zx_pci_init_arg_t*>(buffer.data());      \
  for (unsigned device = 0; device < ZX_PCI_MAX_DEVICES_PER_BUS; ++device) {              \
    for (unsigned function = 0; function < ZX_PCI_MAX_FUNCTIONS_PER_DEVICE; ++function) { \
      for (unsigned pin = 0; pin < ZX_PCI_MAX_LEGACY_IRQ_PINS; ++pin) {                   \
        init_buf->dev_pin_to_global_irq[device][function][pin] =                          \
            pin + function * 16 + device * 256;                                           \
      }                                                                                   \
    }                                                                                     \
  }                                                                                       \
  init_buf->num_irqs = 2;                                                                 \
  init_buf->irqs[0] = {.global_irq = 10, .level_triggered = false, .active_high = true};  \
  init_buf->irqs[1] = {.global_irq = 20, .level_triggered = true, .active_high = false};  \
  init_buf->addr_window_count = 3;                                                        \
  init_buf->addr_windows[0] = {.base = 1000,                                              \
                               .size = 1024,                                              \
                               .bus_start = 1,                                            \
                               .bus_end = 2,                                              \
                               .cfg_space_type = 3,                                       \
                               .has_ecam = false};                                        \
  init_buf->addr_windows[1] = {.base = 2000,                                              \
                               .size = 2024,                                              \
                               .bus_start = 21,                                           \
                               .bus_end = 22,                                             \
                               .cfg_space_type = 23,                                      \
                               .has_ecam = true};                                         \
  init_buf->addr_windows[2] = {.base = 3000,                                              \
                               .size = 3024,                                              \
                               .bus_start = 31,                                           \
                               .bus_end = 32,                                             \
                               .cfg_space_type = 33,                                      \
                               .has_ecam = false};                                        \
  PerformDisplayTest("$plt(zx_pci_init)",                                                 \
                     ZxPciInit(result, #result, kHandle, init_buf, buffer.size()), expected);

#define PCI_INIT_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { PCI_INIT_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { PCI_INIT_DISPLAY_TEST_CONTENT(errno, expected); }

std::string FillPins(std::string_view header, std::string_view footer) {
  std::stringstream stream;
  stream << header;
  std::vector<char> buffer(sizeof(uint32_t) * kCharactersPerByte + 1);
  const char* separator = "";
  int printed_elems = 0;
  for (unsigned device = 0; device < ZX_PCI_MAX_DEVICES_PER_BUS; ++device) {
    for (unsigned function = 0; function < ZX_PCI_MAX_FUNCTIONS_PER_DEVICE; ++function) {
      for (unsigned pin = 0; pin < ZX_PCI_MAX_LEGACY_IRQ_PINS; ++pin) {
        ++printed_elems;
        snprintf(buffer.data(), buffer.size(), "%08x", pin + function * 16 + device * 256);
        stream << separator << "\x1B[34m" << buffer.data() << "\x1B[0m";
        if (printed_elems == 12) {
          printed_elems = 0;
          separator = "\n      ";
        } else {
          separator = ", ";
        }
      }
    }
  }
  stream << "\n";
  stream << footer;
  return stream.str();
}

PCI_INIT_DISPLAY_TEST(
    ZxPciInit, ZX_OK,
    FillPins("\n"
             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_pci_init("
             "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
             "len: \x1B[32muint32\x1B[0m = \x1B[34m5968\x1B[0m)\n"
             "  init_buf: \x1B[32mzx_pci_init_arg_t\x1B[0m = {\n"
             "    dev_pin_to_global_irq: array<\x1B[32muint32\x1B[0m> = [\n      ",
             "    ]\n"
             "    num_irqs: \x1B[32muint32\x1B[0m = \x1B[34m2\x1B[0m\n"
             "    irqs: vector<\x1B[32mzx_pci_init_arg_irq_t\x1B[0m> = [\n"
             "      { "
             "global_irq: \x1B[32muint32\x1B[0m = \x1B[34m10\x1B[0m, "
             "level_triggered: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m, "
             "active_high: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m"
             " }\n"
             "      { "
             "global_irq: \x1B[32muint32\x1B[0m = \x1B[34m20\x1B[0m, "
             "level_triggered: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m, "
             "active_high: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m"
             " }\n"
             "    ]\n"
             "    addr_window_count: \x1B[32muint32\x1B[0m = \x1B[34m3\x1B[0m\n"
             "    addr_windows: vector<\x1B[32mzx_pci_init_arg_addr_window_t\x1B[0m> = [\n"
             "      {\n"
             "        base: \x1B[32muint64\x1B[0m = \x1B[34m1000\x1B[0m\n"
             "        size: \x1B[32msize\x1B[0m = \x1B[34m1024\x1B[0m\n"
             "        bus_start: \x1B[32muint8\x1B[0m = \x1B[34m1\x1B[0m\n"
             "        bus_end: \x1B[32muint8\x1B[0m = \x1B[34m2\x1B[0m\n"
             "        cfg_space_type: \x1B[32muint8\x1B[0m = \x1B[34m3\x1B[0m\n"
             "        has_ecam: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m\n"
             "      }\n"
             "      {\n"
             "        base: \x1B[32muint64\x1B[0m = \x1B[34m2000\x1B[0m\n"
             "        size: \x1B[32msize\x1B[0m = \x1B[34m2024\x1B[0m\n"
             "        bus_start: \x1B[32muint8\x1B[0m = \x1B[34m21\x1B[0m\n"
             "        bus_end: \x1B[32muint8\x1B[0m = \x1B[34m22\x1B[0m\n"
             "        cfg_space_type: \x1B[32muint8\x1B[0m = \x1B[34m23\x1B[0m\n"
             "        has_ecam: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m\n"
             "      }\n"
             "      {\n"
             "        base: \x1B[32muint64\x1B[0m = \x1B[34m3000\x1B[0m\n"
             "        size: \x1B[32msize\x1B[0m = \x1B[34m3024\x1B[0m\n"
             "        bus_start: \x1B[32muint8\x1B[0m = \x1B[34m31\x1B[0m\n"
             "        bus_end: \x1B[32muint8\x1B[0m = \x1B[34m32\x1B[0m\n"
             "        cfg_space_type: \x1B[32muint8\x1B[0m = \x1B[34m33\x1B[0m\n"
             "        has_ecam: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m\n"
             "      }\n"
             "    ]\n"
             "  }\n"
             "  -> \x1B[32mZX_OK\x1B[0m\n")
        .c_str());

// zx_pci_add_subtract_io_range tests.

std::unique_ptr<SystemCallTest> ZxPciAddSubtractIoRange(int64_t result,
                                                        std::string_view result_name,
                                                        zx_handle_t handle, bool mmio,
                                                        uint64_t base, uint64_t len, bool add) {
  auto value =
      std::make_unique<SystemCallTest>("zx_pci_add_subtract_io_range", result, result_name);
  value->AddInput(handle);
  value->AddInput(mmio);
  value->AddInput(base);
  value->AddInput(len);
  value->AddInput(add);
  return value;
}

#define PCI_ADD_SUBTRACT_IO_RANGE_DISPLAY_TEST_CONTENT(result, expected)                         \
  PerformDisplayTest("$plt(zx_pci_add_subtract_io_range)",                                       \
                     ZxPciAddSubtractIoRange(result, #result, kHandle, true, 1000, 1024, false), \
                     expected);

#define PCI_ADD_SUBTRACT_IO_RANGE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                         \
    PCI_ADD_SUBTRACT_IO_RANGE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                   \
  TEST_F(InterceptionWorkflowTestArm, name) {                         \
    PCI_ADD_SUBTRACT_IO_RANGE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PCI_ADD_SUBTRACT_IO_RANGE_DISPLAY_TEST(ZxPciAddSubtractIoRange, ZX_OK,
                                       "\n"
                                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                       "zx_pci_add_subtract_io_range("
                                       "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                                       "mmio: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m, "
                                       "base: \x1B[32muint64\x1B[0m = \x1B[34m1000\x1B[0m, "
                                       "len: \x1B[32muint64\x1B[0m = \x1B[34m1024\x1B[0m, "
                                       "add: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m)\n"
                                       "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
