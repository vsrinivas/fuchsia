// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-thermal.h"

#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <fbl/unique_ptr.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mtk-thermal-reg.h"

namespace {

constexpr uint32_t kTsCon1Addr = 0x10018604;
constexpr uint32_t kAuxAdcCon1SetAddr = 0x11003008;
constexpr uint32_t kAuxAdcCon1ClrAddr = 0x1100300c;
constexpr uint32_t kAuxAdcDat11Addr = 0x11003040;
constexpr uint32_t kAuxAdcChannel = 11;
constexpr uint32_t kAuxAdcBits = 12;

constexpr int kSensorCount = 3;

constexpr uint32_t kKelvinOffset = 2732;  // Units: 0.1 degrees C

// TODO(bradenkell): Figure out what the actual time base is (66 MHz or 32 kHz?) and calculate
//                   these instead of hard coding.
constexpr uint32_t kThermalPeriod = 12;
constexpr uint32_t kSenseInterval = 429;
constexpr uint32_t kAhbPollPeriod = 768;

int32_t FixedPoint(int32_t value) {
    return (value * 10000) >> 12;
}

int32_t RawWithGain(int32_t raw, int32_t gain) {
    return (FixedPoint(raw) * 10000) / gain;
}

}  // namespace

namespace thermal {

zx_status_t MtkThermal::Create(zx_device_t* parent) {
    zx_status_t status;

    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    ddk::ClkProtocolClient clk(parent);
    if (!clk.is_valid()) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_CLK not available\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    pdev_device_info_t info;
    if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_device_info failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> fuse_mmio;
    if ((status = pdev.MapMmio(1, &fuse_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> pll_mmio;
    if ((status = pdev.MapMmio(2, &pll_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> pmic_mmio;
    if ((status = pdev.MapMmio(3, &pmic_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    thermal_device_info_t thermal_info;
    size_t actual;
    status = device_get_metadata(parent, THERMAL_CONFIG_METADATA, &thermal_info,
                                 sizeof(thermal_info), &actual);
    if (status != ZX_OK || actual != sizeof(thermal_info)) {
        zxlogf(ERROR, "%s: device_get_metadata failed\n", __FILE__);
        return status == ZX_OK ? ZX_ERR_INTERNAL : status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<MtkThermal> device(
        new (&ac) MtkThermal(parent, std::move(*mmio), std::move(*fuse_mmio), std::move(*pll_mmio),
                             std::move(*pmic_mmio), clk, info, thermal_info));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: MtkThermal alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->Init()) != ZX_OK) {
        return status;
    }

    if ((status = device->DdkAdd("mtk-thermal")) != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

zx_status_t MtkThermal::Init() {
    for (uint32_t i = 0; i < clk_count_; i++) {
        zx_status_t status = clk_.Enable(i);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to enable clock %u\n", __FILE__, i);
            return status;
        }
    }

    // Set the initial DVFS operating point. The bootloader sets it to 1.001 GHz @ 1.2 V.
    constexpr dvfs_info_t dvfs_info = {
        .op_idx = 0,
        .power_domain = BIG_CLUSTER_POWER_DOMAIN
    };

    zx_status_t status = SetDvfsOpp(&dvfs_info);
    if (status != ZX_OK) {
        return status;
    }

    TempMonCtl0::Get().ReadFrom(&mmio_).disable_all().WriteTo(&mmio_);

    TempMsrCtl0::Get()
        .ReadFrom(&mmio_)
        .set_msrctl0(TempMsrCtl0::kSample1)
        .set_msrctl1(TempMsrCtl0::kSample1)
        .set_msrctl2(TempMsrCtl0::kSample1)
        .set_msrctl3(TempMsrCtl0::kSample1)
        .WriteTo(&mmio_);

    TempAhbTimeout::Get().FromValue(0xffffffff).WriteTo(&mmio_);
    TempAdcPnp::Get(0).FromValue(0).WriteTo(&mmio_);
    TempAdcPnp::Get(1).FromValue(1).WriteTo(&mmio_);
    TempAdcPnp::Get(2).FromValue(2).WriteTo(&mmio_);

    // Set the thermal controller to read from the spare registers, then wait for the dummy sensor
    // reading to end up in TempMsr0-2.
    TempMonCtl1::Get().ReadFrom(&mmio_).set_period(1).WriteTo(&mmio_);
    TempMonCtl2::Get().ReadFrom(&mmio_).set_sen_interval(1).WriteTo(&mmio_);
    TempAhbPoll::Get().FromValue(1).WriteTo(&mmio_);

    constexpr uint32_t dummy_temp = (1 << kAuxAdcBits) - 1;
    TempSpare::Get(0).FromValue(dummy_temp | (1 << kAuxAdcBits)).WriteTo(&mmio_);

    TempPnpMuxAddr::Get().FromValue(TempSpare::Get(2).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
    TempAdcMuxAddr::Get().FromValue(TempSpare::Get(2).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
    TempAdcEnAddr::Get().FromValue(TempSpare::Get(1).addr() + MT8167_THERMAL_BASE).WriteTo(&mmio_);
    TempAdcValidAddr::Get()
        .FromValue(TempSpare::Get(0).addr() + MT8167_THERMAL_BASE)
        .WriteTo(&mmio_);
    TempAdcVoltAddr::Get()
        .FromValue(TempSpare::Get(0).addr() + MT8167_THERMAL_BASE)
        .WriteTo(&mmio_);

    TempRdCtrl::Get().ReadFrom(&mmio_).set_diff(TempRdCtrl::kValidVoltageSame).WriteTo(&mmio_);
    TempAdcValidMask::Get()
        .ReadFrom(&mmio_)
        .set_polarity(TempAdcValidMask::kActiveHigh)
        .set_pos(kAuxAdcBits)
        .WriteTo(&mmio_);
    TempAdcVoltageShift::Get().FromValue(0).WriteTo(&mmio_);
    TempMonCtl0::Get().ReadFrom(&mmio_).enable_all().WriteTo(&mmio_);

    for (int i = 0; i < kSensorCount; i++) {
        auto msr = TempMsr::Get(i).ReadFrom(&mmio_);
        for (; msr.valid() == 0 || msr.reading() != dummy_temp; msr.ReadFrom(&mmio_)) {}
    }

    TempMonCtl0::Get().ReadFrom(&mmio_).disable_all().WriteTo(&mmio_);

    // Set the thermal controller to get temperature readings from the aux ADC.
    TempMonCtl1::Get().ReadFrom(&mmio_).set_period(kThermalPeriod).WriteTo(&mmio_);
    TempMonCtl2::Get()
        .ReadFrom(&mmio_)
        .set_sen_interval(kSenseInterval)
        .set_filt_interval(1)
        .WriteTo(&mmio_);
    TempAhbPoll::Get().FromValue(kAhbPollPeriod).WriteTo(&mmio_);

    TempAdcEn::Get().FromValue(1 << kAuxAdcChannel).WriteTo(&mmio_);
    TempAdcMux::Get().FromValue(1 << kAuxAdcChannel).WriteTo(&mmio_);

    TempPnpMuxAddr::Get().FromValue(kTsCon1Addr).WriteTo(&mmio_);
    TempAdcEnAddr::Get().FromValue(kAuxAdcCon1SetAddr).WriteTo(&mmio_);
    TempAdcMuxAddr::Get().FromValue(kAuxAdcCon1ClrAddr).WriteTo(&mmio_);
    TempAdcValidAddr::Get().FromValue(kAuxAdcDat11Addr).WriteTo(&mmio_);
    TempAdcVoltAddr::Get().FromValue(kAuxAdcDat11Addr).WriteTo(&mmio_);

    TempAdcWriteCtrl::Get()
        .ReadFrom(&mmio_)
        .set_mux_write_en(1)
        .set_pnp_write_en(1)
        .WriteTo(&mmio_);

    TempMonCtl0::Get().ReadFrom(&mmio_).enable_real().WriteTo(&mmio_);

    return ZX_OK;
}

uint16_t MtkThermal::PmicRead(uint32_t addr) {
    while (PmicReadData::Get().ReadFrom(&pmic_mmio_).status() != PmicReadData::kStateIdle) {}

    PmicCmd::Get().FromValue(0).set_write(0).set_addr(addr).WriteTo(&pmic_mmio_);

    auto pmic_read = PmicReadData::Get().FromValue(0);
    while (pmic_read.ReadFrom(&pmic_mmio_).status() != PmicReadData::kStateValid) {}

    uint16_t ret = static_cast<uint16_t>(pmic_read.data());

    PmicValidClear::Get().ReadFrom(&pmic_mmio_).set_valid_clear(1).WriteTo(&pmic_mmio_);

    return ret;
}

void MtkThermal::PmicWrite(uint16_t data, uint32_t addr) {
    while (PmicReadData::Get().ReadFrom(&pmic_mmio_).status() != PmicReadData::kStateIdle) {}
    PmicCmd::Get().FromValue(0).set_write(1).set_addr(addr).set_data(data).WriteTo(&pmic_mmio_);
}

uint32_t MtkThermal::RawToTemperature(uint32_t raw, int sensor) {
    auto cal0 = TempCalibration0::Get().ReadFrom(&fuse_mmio_);
    auto cal1 = TempCalibration1::Get().ReadFrom(&fuse_mmio_);
    auto cal2 = TempCalibration2::Get().ReadFrom(&fuse_mmio_);

    int32_t vts = cal2.get_vts3();
    if (sensor == 0) {
        vts = cal0.get_vts0();
    } else if (sensor == 1) {
        vts = cal0.get_vts1();
    } else if (sensor == 2) {
        vts = cal2.get_vts2();
    }

    // See misc/mediatek/thermal/mt8167/mtk_ts_cpu.c in the Linux kernel source.
    int32_t gain = 10000 + FixedPoint(cal1.get_adc_gain());
    int32_t vts_with_gain = RawWithGain(vts - cal1.get_adc_offset(), gain);
    int32_t temp_c = ((RawWithGain(raw - cal1.get_adc_offset(), gain) - vts_with_gain) * 5) / 6;
    int32_t slope = cal0.slope_sign() == 0 ? cal0.slope() : -cal0.slope();
    temp_c = cal0.temp_offset() - ((temp_c * 100) / (165 + (cal1.id() == 0 ? 0 : slope)));
    return temp_c + kKelvinOffset;
}

zx_status_t MtkThermal::GetTemperature(uint32_t* temp) {
    *temp = 0;
    for (int i = 0; i < kSensorCount; i++) {
        auto msr = TempMsr::Get(i).ReadFrom(&mmio_);
        if (!msr.valid()) {
            continue;
        }

        uint32_t sensor_temp = RawToTemperature(msr.reading(), i);
        if (sensor_temp > *temp) {
            *temp = sensor_temp;
        }
    }

    return ZX_OK;
}

zx_status_t MtkThermal::SetDvfsOpp(const dvfs_info_t* opp) {
    if (opp->power_domain >= MAX_DVFS_DOMAINS) {
        return ZX_ERR_INVALID_ARGS;
    }

    const scpi_opp_t& opps = thermal_info_.opps[opp->power_domain];
    if (opp->op_idx >= opps.count) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uint32_t new_freq = opps.opp[opp->op_idx].freq_hz;
    uint32_t new_volt = opps.opp[opp->op_idx].volt_mv;

    if (new_volt > VprocCon10::kMaxVoltageUv || new_volt < VprocCon10::kMinVoltageUv) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    auto armpll = ArmPllCon1::Get().ReadFrom(&pll_mmio_);
    uint32_t old_freq = armpll.frequency();

    auto vproc = VprocCon10::Get().FromValue(0).set_voltage(new_volt);
    if (vproc.voltage() != new_volt) {
        // The requested voltage is not a multiple of the voltage step.
        return ZX_ERR_INVALID_ARGS;
    }

    if (new_freq > old_freq) {
        PmicWrite(vproc.reg_value(), vproc.reg_addr());
        armpll.set_frequency(new_freq).WriteTo(&pll_mmio_);
    } else {
        armpll.set_frequency(new_freq).WriteTo(&pll_mmio_);
        PmicWrite(vproc.reg_value(), vproc.reg_addr());
    }

    current_opp_idx_ = opp->op_idx;

    return ZX_OK;
}

zx_status_t MtkThermal::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                 size_t out_len, size_t* actual) {
    switch (op) {
    case IOCTL_THERMAL_GET_TEMPERATURE:
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        *actual = sizeof(uint32_t);
        return GetTemperature(reinterpret_cast<uint32_t*>(out_buf));

    case IOCTL_THERMAL_GET_DEVICE_INFO:
        if (out_len != sizeof(thermal_info_)) {
            return ZX_ERR_INVALID_ARGS;
        }

        memcpy(out_buf, &thermal_info_, sizeof(thermal_info_));
        *actual = sizeof(thermal_info_);
        return ZX_OK;

    case IOCTL_THERMAL_SET_DVFS_OPP:
        if (in_len != sizeof(dvfs_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        return SetDvfsOpp(reinterpret_cast<const dvfs_info_t*>(in_buf));

    case IOCTL_THERMAL_GET_DVFS_INFO: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(thermal_info_.opps[0])) {
            return ZX_ERR_INVALID_ARGS;
        }

        uint32_t domain = *reinterpret_cast<const uint32_t*>(in_buf);
        if (domain >= MAX_DVFS_DOMAINS) {
            return ZX_ERR_INVALID_ARGS;
        }

        memcpy(out_buf, &thermal_info_.opps[domain], sizeof(thermal_info_.opps[0]));
        *actual = sizeof(thermal_info_.opps[0]);
        return ZX_OK;
    }

    case IOCTL_THERMAL_GET_DVFS_OPP: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        uint32_t domain = *reinterpret_cast<const uint32_t*>(in_buf);
        if (domain != BIG_CLUSTER_POWER_DOMAIN) {
            return ZX_ERR_INVALID_ARGS;
        }

        uint32_t* opp_idx = reinterpret_cast<uint32_t*>(out_buf);

        *opp_idx = current_opp_idx_;
        *actual = sizeof(*opp_idx);
        return ZX_OK;
    }

    // TODO(bradenkell): Implement the rest of these.
    case IOCTL_THERMAL_GET_INFO:
    case IOCTL_THERMAL_SET_TRIP:
    case IOCTL_THERMAL_GET_STATE_CHANGE_EVENT:
    case IOCTL_THERMAL_GET_STATE_CHANGE_PORT:
    case IOCTL_THERMAL_SET_FAN_LEVEL:
    case IOCTL_THERMAL_GET_FAN_LEVEL:
    default:
        break;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace thermal

extern "C" zx_status_t mtk_thermal_bind(void* ctx, zx_device_t* parent) {
    return thermal::MtkThermal::Create(parent);
}
