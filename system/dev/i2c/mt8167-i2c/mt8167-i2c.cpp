// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167-i2c.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c-impl.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-device-lib.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>

#include <zircon/syscalls/port.h>
#include <zircon/types.h>

//#define TEST_USB_REGS_READ

// clang-format off
#define REG_R32(reg) reg::Get().ReadFrom(&keys_[id].mmio).reg_value()

#define REG_W32(reg, val)                                               \
    reg::Get().FromValue(0).                                            \
    set_reg_value(static_cast<uint32_t>(val)).                          \
    WriteTo(&keys_[id].mmio)

#define REG_R(reg, field) reg::Get().ReadFrom(&keys_[id].mmio).field()

#define REG_RMW(reg, field, val)                                        \
    reg::Get().ReadFrom(&keys_[id].mmio).                               \
    set_ ## field(static_cast<uint32_t>(val)).                          \
    WriteTo(&keys_[id].mmio)

#define REG_RMMW(reg, field1, val1, field2, val2)                       \
    reg::Get().ReadFrom(&keys_[id].mmio).                               \
    set_ ## field1(static_cast<uint32_t>(val1)).                        \
    set_ ## field2(static_cast<uint32_t>(val2)).                        \
    WriteTo(&keys_[id].mmio)
// clang-format on

namespace mt8167_i2c {

constexpr size_t kMaxTransferSize = UINT16_MAX - 1; // More than enough.
constexpr size_t kHwFifoSize = 8;
constexpr uint32_t kEventCompletion = ZX_USER_SIGNAL_0;
constexpr zx::duration kTimeout = zx::msec(10);

uint32_t Mt8167I2c::I2cImplGetBusCount() {
    return bus_count_;
}

zx_status_t Mt8167I2c::I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
    *out_size = kMaxTransferSize;
    return ZX_OK;
}

zx_status_t Mt8167I2c::I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) {
    // TODO(andresoportus): Support changing frequencies.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167I2c::I2cImplTransact(uint32_t id, const i2c_impl_op_t* ops, size_t count) {
    zx_status_t status = ZX_OK;
    if (id >= bus_count_) {
        return ZX_ERR_INVALID_ARGS;
    }

    REG_RMMW(ControlReg, ackerr_det_en, 1, clk_ext_en, 1);

    for (size_t i = 0; i < count; ++i) {
        if (ops[i].address > 0xFF) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        uint8_t addr = static_cast<uint8_t>(ops[i].address);
        // TODO(andresoportus): Add support for HW transaction (write followed by read).
        status = Transact(ops[i].is_read, id, addr, ops[i].data_buffer, ops[i].data_size,
                          ops[i].stop);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: status %d\n", __FUNCTION__, status);
            Reset(id);
            return status;
        }
    }

    return ZX_OK;
}

int Mt8167I2c::IrqThread() {
    zx_port_packet_t packet;
    while (1) {
        auto status = irq_port_.wait(zx::time::infinite(), &packet);
        zxlogf(TRACE, "Port key %lu triggered\n", packet.key);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: irq_port_.wait failed %d \n", __func__, status);
            return status;
        }
        auto id = static_cast<uint32_t>(packet.key);
        ZX_ASSERT(id < keys_.size());
        keys_[id].irq.ack();
        auto st = IntrStatReg::Get().ReadFrom(&keys_[id].mmio);
        if (st.arb_lost() || st.hs_nacker() || st.ackerr()) {
            zxlogf(ERROR, "%s: error 0x%08X\n", __func__, REG_R32(IntrStatReg));
            IntrStatReg::Get().ReadFrom(&keys_[id].mmio).Print();
        }
        keys_[id].event.signal(0, kEventCompletion);
    }
}

void Mt8167I2c::Reset(uint32_t id) {
    REG_RMW(SoftResetReg, soft_reset, 1);
    REG_W32(IntrStatReg, 0xFFFFFFFF); // Write to clear register.
}

void Mt8167I2c::DataMove(bool is_read, uint32_t id, void* buf, size_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) {
        if (is_read) {
            p[i] = static_cast<uint8_t>(REG_R(DataPortReg, data_port));
        } else {
            REG_RMW(DataPortReg, data_port, p[i]);
        }
    }
}

zx_status_t Mt8167I2c::Transact(bool is_read, uint32_t id, uint8_t addr, void* buf,
                                size_t len, bool stop) {
    zx_status_t status;

    // TODO(andresoportus): Only stop when stop is set.
    // TODO(andresoportus): Add support for arbitrary sizes.
    if (len > kHwFifoSize) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint8_t addr_dir = static_cast<uint8_t>((addr << 1) | is_read);
    REG_RMW(FifoAddrClrReg, fifo_addr_clr, 1);
    REG_RMW(SlaveAddrReg, slave_addr, addr_dir);
    REG_RMW(TransacLenReg, transfer_len, len);

    REG_W32(IntrStatReg, 0xFFFFFFFF); // Write to clear register.

    if (!is_read) {
        DataMove(is_read, id, buf, len);
    }

    REG_RMW(StartReg, start, 1);
    status = keys_[id].event.wait_one(kEventCompletion, zx::deadline_after(kTimeout), nullptr);
    if (status != ZX_OK) {
        return status;
    }
    status = keys_[id].event.signal(kEventCompletion, 0);
    if (status != ZX_OK) {
        return status;
    }
    if (is_read) {
        DataMove(is_read, id, buf, len);
    }
    auto st = IntrStatReg::Get().ReadFrom(&keys_[id].mmio);
    if (st.arb_lost() || st.hs_nacker() || st.ackerr()) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

void Mt8167I2c::ShutDown() {
    for (uint32_t id = 0; id < bus_count_; id++) {
        keys_[id].irq.destroy();
    }
    thrd_join(irq_thread_, NULL);
}

void Mt8167I2c::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Mt8167I2c::DdkRelease() {
    delete this;
}

int Mt8167I2c::TestThread() {
#ifdef TEST_USB_REGS_READ
    constexpr uint32_t bus_id = 2;
    constexpr uint8_t addr = 0x48;
    Reset(bus_id);
    for (uint8_t data_write = 0; data_write < 0xF; ++data_write) {
        uint8_t data_read;
        i2c_impl_op_t ops[] = {
            {.address = addr,
             .data_buffer = &data_write,
             .data_size = 1,
             .is_read = false,
             .stop = false},
            {.address = addr,
             .data_buffer = &data_read,
             .data_size = 1,
             .is_read = true,
             .stop = true},
        };
        auto status = I2cImplTransact(bus_id, ops, countof(ops));
        if (status == ZX_OK) {
            zxlogf(INFO, "I2C Addr: 0x%02X Reg:0x%02X Value:0x%02X\n", addr, data_write, data_read);
        }
    }
#endif
    return 0;
}

zx_status_t Mt8167I2c::Bind() {
    zx_status_t status;

    status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &irq_port_);
    if (status != ZX_OK) {
        return status;
    }

    pdev_protocol_t pdev;
    status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s ZX_PROTOCOL_PLATFORM_DEV failed %d\n", __FUNCTION__, status);
        return ZX_ERR_NOT_SUPPORTED;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_get_device_info failed %d\n", __FUNCTION__, status);
        return ZX_ERR_NOT_SUPPORTED;
    }

    bus_count_ = info.mmio_count - 1; // Last MMIO is for XO clock.
    if (bus_count_ != MT8167_I2C_CNT) {
        zxlogf(ERROR, "%s wrong I2C count %d\n", __FUNCTION__, bus_count_);
        return ZX_ERR_INTERNAL;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev, bus_count_, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }
    xo_regs_ = XoRegs(mmio); // Last MMIO is for XO clock.

    for (uint32_t id = 0; id < bus_count_; id++) {
        status = pdev_map_mmio_buffer2(&pdev, id, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
            return status;
        }

        zx::event event;
        status = zx::event::create(0, &event);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s zx::event::create failed %d\n", __FUNCTION__, status);
            return status;
        }
        keys_.push_back({ddk::MmioBuffer(mmio), zx::interrupt(), fbl::move(event)});

        status = pdev_map_interrupt(&pdev, id, keys_[id].irq.reset_and_get_address());
        if (status != ZX_OK) {
            return status;
        }
        status = keys_[id].irq.bind(irq_port_.get(), id, 0); // id is the port key used.
        if (status != ZX_OK) {
            return status;
        }

        // TODO(andresoportus): Add support for turn on only during transactions?.
        xo_regs_.value().ClockEnable(id, true);

        // TODO(andresoportus): Add support for DMA mode.
    }

    auto thunk = [](void* arg) -> int { return reinterpret_cast<Mt8167I2c*>(arg)->IrqThread(); };
    int rc = thrd_create_with_name(&irq_thread_, thunk, this, "mt8167-i2c");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    status = DdkAdd("mt8167-i2c");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed: %d\n", __FUNCTION__, status);
        ShutDown();
    }
    return status;
}

zx_status_t Mt8167I2c::Init() {
    auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

    pbus_protocol_t pbus;
    if (device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus) != ZX_OK) {
        zxlogf(ERROR, "%s ZX_PROTOCOL_PLATFORM_BUS not available\n", __FUNCTION__);
        return ZX_ERR_NOT_SUPPORTED;
    }
    i2c_impl_protocol_t i2c_proto = {
        .ops = &ops_,
        .ctx = this,
    };
    const platform_proxy_cb_t kCallback = {NULL, NULL};
    auto status = pbus_register_protocol(&pbus, ZX_PROTOCOL_I2C_IMPL, &i2c_proto, sizeof(i2c_proto),
                                         &kCallback);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed: %d\n", __FUNCTION__, status);
        return status;
    }

#ifdef TEST_USB_REGS_READ
    auto thunk =
        [](void* arg) -> int { return reinterpret_cast<Mt8167I2c*>(arg)->TestThread(); };
    int rc = thrd_create_with_name(&irq_thread_, thunk, this, "mt8167-i2c-test");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
#endif

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Mt8167I2c::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<Mt8167I2c>(&ac, parent);
    if (!ac.check()) {
        zxlogf(ERROR, "%s ZX_ERR_NO_MEMORY\n", __FUNCTION__);
        return ZX_ERR_NO_MEMORY;
    }

    auto status = dev->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev
    auto ptr = dev.release();

    return ptr->Init();
}

} // namespace mt8167_i2c

extern "C" zx_status_t mt8167_i2c_bind(void* ctx, zx_device_t* parent) {
    return mt8167_i2c::Mt8167I2c::Create(parent);
}
