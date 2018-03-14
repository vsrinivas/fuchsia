// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <lib/edid/edid.h>

#include "intel-i915.h"
#include "hdmi-display.h"
#include "macros.h"
#include "pci-ids.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

// I2c functions

namespace {
// Recommended DDI buffer translation programming values

struct ddi_buf_trans_entry {
    uint32_t high_dword;
    uint32_t low_dword;
};

const ddi_buf_trans_entry hdmi_ddi_buf_trans_skl_uhs[11] {
    { 0x000000ac, 0x00000018 },
    { 0x0000009d, 0x00005012 },
    { 0x00000088, 0x00007011 },
    { 0x000000a1, 0x00000018 },
    { 0x00000098, 0x00000018 },
    { 0x00000088, 0x00004013 },
    { 0x000000cd, 0x80006012 },
    { 0x000000df, 0x00000018 },
    { 0x000000cd, 0x80003015 },
    { 0x000000c0, 0x80003015 },
    { 0x000000c0, 0x80000018 },
};

const ddi_buf_trans_entry hdmi_ddi_buf_trans_skl_y[11] {
    { 0x000000a1, 0x00000018 },
    { 0x000000df, 0x00005012 },
    { 0x000000cb, 0x80007011 },
    { 0x000000a4, 0x00000018 },
    { 0x0000009d, 0x00000018 },
    { 0x00000080, 0x00004013 },
    { 0x000000c0, 0x80006012 },
    { 0x0000008a, 0x00000018 },
    { 0x000000c0, 0x80003015 },
    { 0x000000c0, 0x80003015 },
    { 0x000000c0, 0x80000018 },
};

int ddi_to_pin(registers::Ddi ddi) {
    if (ddi == registers::DDI_B) {
        return registers::GMBus0::kDdiBPin;
    } else if (ddi == registers::DDI_C) {
        return registers::GMBus0::kDdiCPin;
    } else if (ddi == registers::DDI_D) {
        return registers::GMBus0::kDdiDPin;
    }
    return -1;
}

void write_gmbus3(hwreg::RegisterIo* mmio_space, uint8_t* buf, uint32_t size, uint32_t idx) {
    int cur_byte = 0;
    uint32_t val = 0;
    while (idx < size && cur_byte < 4) {
        val |= buf[idx++] << (8 * cur_byte++);
    }
    registers::GMBus3::Get().FromValue(val).WriteTo(mmio_space);
}

void read_gmbus3(hwreg::RegisterIo* mmio_space, uint8_t* buf, uint32_t size, uint32_t idx) {
    int cur_byte = 0;
    uint32_t val = registers::GMBus3::Get().ReadFrom(mmio_space).reg_value();
    while (idx < size && cur_byte++ < 4) {
        buf[idx++] = val & 0xff;
        val >>= 8;
    }
}

static constexpr uint8_t kI2cClockUs = 10; // 100 kHz

// For bit banging i2c over the gpio pins
bool i2c_scl(hwreg::RegisterIo* mmio_space, registers::Ddi ddi, bool hi) {
    auto gpio = registers::GpioCtl::Get(ddi).FromValue(0);

    if (!hi) {
        gpio.set_clock_direction_val(1);
        gpio.set_clock_mask(1);
    }
    gpio.set_clock_direction_mask(1);

    gpio.WriteTo(mmio_space);
    gpio.ReadFrom(mmio_space); // Posting read

    // Handle the case where something on the bus is holding the clock
    // low. Timeout after 1ms.
    if (hi) {
        int count = 0;
        do {
            if (count != 0) {
                zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs)));
            }
            gpio.ReadFrom(mmio_space);
        } while (count++ < 100 && hi != gpio.clock_in());
        if (hi != gpio.clock_in()) {
            return false;
        }
    }
    zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs / 2)));
    return true;
}

// For bit banging i2c over the gpio pins
void i2c_sda(hwreg::RegisterIo* mmio_space, registers::Ddi ddi, bool hi) {
    auto gpio = registers::GpioCtl::Get(ddi).FromValue(0);

    if (!hi) {
        gpio.set_data_direction_val(1);
        gpio.set_data_mask(1);
    }
    gpio.set_data_direction_mask(1);

    gpio.WriteTo(mmio_space);
    gpio.ReadFrom(mmio_space); // Posting read

    zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs / 2)));
}

// For bit banging i2c over the gpio pins
bool i2c_send_byte(hwreg::RegisterIo* mmio_space, registers::Ddi ddi, uint8_t byte) {
    // Set the bits from MSB to LSB
    for (int i = 7; i >= 0; i--) {
        i2c_sda(mmio_space, ddi, (byte >> i) & 0x1);

        i2c_scl(mmio_space, ddi, 1);

        // Leave the data line where it is for the rest of the cycle
        zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs / 2)));

        i2c_scl(mmio_space, ddi, 0);
    }

    // Release the data line and check for an ack
    i2c_sda(mmio_space, ddi, 1);
    i2c_scl(mmio_space, ddi, 1);

    bool ack = !registers::GpioCtl::Get(ddi).ReadFrom(mmio_space).data_in();

    // Sleep for the rest of the cycle
    zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs / 2)));

    i2c_scl(mmio_space, ddi, 0);

    return ack;
}

} // namespace

namespace i915 {

HdmiDisplay::HdmiDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe)
        : DisplayDevice(controller, ddi, static_cast<registers::Trans>(pipe), pipe) { }

// Per the GMBUS Controller Programming Interface section of the Intel docs, GMBUS does not
// directly support segment pointer addressing. Instead, the segment pointer needs to be
// set by bit-banging the GPIO pins.
bool HdmiDisplay::SetDdcSegment(uint8_t segment_num) {
    // Reset the clock and data lines
    i2c_scl(mmio_space(), ddi(), 0);
    i2c_sda(mmio_space(), ddi(), 0);

    if (!i2c_scl(mmio_space(), ddi(), 1)) {
        return false;
    }
    i2c_sda(mmio_space(), ddi(), 1);
    // Wait for the rest of the cycle
    zx_nanosleep(zx_deadline_after(ZX_USEC(kI2cClockUs / 2)));

    // Send a start condition
    i2c_sda(mmio_space(), ddi(), 0);
    i2c_scl(mmio_space(), ddi(), 0);

    // Send the segment register index and the segment number
    uint8_t segment_write_command = kDdcSegmentI2cAddress << 1;
    if (!i2c_send_byte(mmio_space(), ddi(), segment_write_command)
            || !i2c_send_byte(mmio_space(), ddi(), segment_num)) {
        return false;
    }

    // Set the data and clock lines high to prepare for the GMBus start
    i2c_sda(mmio_space(), ddi(), 1);
    return i2c_scl(mmio_space(), ddi(), 1);
}

bool HdmiDisplay::DdcRead(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) {
    registers::GMBus0::Get().FromValue(0).WriteTo(mmio_space());

    int retries = 0;
    int i = 0;
    while (i < 3) {
        bool success;
        if (i == 0) {
            success = segment == 0 || SetDdcSegment(segment);
        } else {
            if (i == 1) {
                auto gmbus0 = registers::GMBus0::Get().FromValue(0);
                gmbus0.set_pin_pair_select(ddi_to_pin(ddi()));
                gmbus0.WriteTo(mmio_space());

                success = GMBusWrite(kDdcDataI2cAddress, &offset, 1);
            } else {
                success = GMBusRead(kDdcDataI2cAddress, buf, len);
            }
            if (success) {
                if (!WAIT_ON_MS(registers::GMBus2::Get().ReadFrom(mmio_space()).wait(), 10)) {
                    zxlogf(ERROR, "Transition to wait phase timed out\n");
                    success = false;
                }
            }
        }
        if (!success) {
            if (retries++ > 1) {
                zxlogf(ERROR, "Too many block read failures\n");
                return false;
            }
            zxlogf(TRACE, "Block read failed at step %d\n", i);
            i = 0;
            if (!I2cClearNack()) {
                zxlogf(ERROR, "Failed to clear nack\n");
                return false;
            }
        } else {
            i++;
        }
    }

    return I2cFinish() && i == 3;
}

bool HdmiDisplay::GMBusWrite(uint8_t addr, uint8_t* buf, uint8_t size) {
    unsigned idx = 0;
    write_gmbus3(mmio_space(), buf, size, idx);
    idx += 4;

    auto gmbus1 = registers::GMBus1::Get().FromValue(0);
    gmbus1.set_sw_ready(1);
    gmbus1.set_bus_cycle_wait(1);
    gmbus1.set_total_byte_count(size);
    gmbus1.set_slave_register_addr(addr);
    gmbus1.WriteTo(mmio_space());

    while (idx < size) {
        if (!I2cWaitForHwReady()) {
            return false;
        }

        write_gmbus3(mmio_space(), buf, size, idx);
        idx += 4;
    }
    // One more wait to ensure we're ready when we leave the function
    return I2cWaitForHwReady();
}

bool HdmiDisplay::GMBusRead(uint8_t addr, uint8_t* buf, uint8_t size) {
    auto gmbus1 = registers::GMBus1::Get().FromValue(0);
    gmbus1.set_sw_ready(1);
    gmbus1.set_bus_cycle_wait(1);
    gmbus1.set_total_byte_count(size);
    gmbus1.set_slave_register_addr(addr);
    gmbus1.set_read_op(1);
    gmbus1.WriteTo(mmio_space());

    unsigned idx = 0;
    while (idx < size) {
        if (!I2cWaitForHwReady()) {
            return false;
        }

        read_gmbus3(mmio_space(), buf, size, idx);
        idx += 4;
    }

    return true;
}

bool HdmiDisplay::I2cFinish() {
    auto gmbus1 = registers::GMBus1::Get().FromValue(0);
    gmbus1.set_bus_cycle_stop(1);
    gmbus1.set_sw_ready(1);
    gmbus1.WriteTo(mmio_space());

    bool idle = WAIT_ON_MS(!registers::GMBus2::Get().ReadFrom(mmio_space()).active(), 100);

    auto gmbus0 = registers::GMBus0::Get().FromValue(0);
    gmbus0.set_pin_pair_select(0);
    gmbus0.WriteTo(mmio_space());

    if (!idle) {
        zxlogf(ERROR, "hdmi: GMBus i2c failed to go idle\n");
    }
    return idle;
}

bool HdmiDisplay::I2cWaitForHwReady() {
    auto gmbus2 = registers::GMBus2::Get().FromValue(0);
    if (!WAIT_ON_MS({ gmbus2.ReadFrom(mmio_space()); gmbus2.nack() || gmbus2.hw_ready(); }, 50)) {
        zxlogf(ERROR, "hdmi: GMBus i2c wait for hwready timeout\n");
        return false;
    }
    if (gmbus2.nack()) {
        zxlogf(ERROR, "hdmi: GMBus i2c got nack\n");
        return false;
    }
    return true;
}

bool HdmiDisplay::I2cClearNack() {
    I2cFinish();

    if (!WAIT_ON_MS(!registers::GMBus2::Get().ReadFrom(mmio_space()).active(), 10)) {
        zxlogf(ERROR, "hdmi: GMBus i2c failed to clear active nack\n");
        return false;
    }

    // Set/clear sw clear int to reset the bus
    auto gmbus1 = registers::GMBus1::Get().FromValue(0);
    gmbus1.set_sw_clear_int(1);
    gmbus1.WriteTo(mmio_space());
    gmbus1.set_sw_clear_int(0);
    gmbus1.WriteTo(mmio_space());

    // Reset GMBus0
    auto gmbus0 = registers::GMBus0::Get().FromValue(0);
    gmbus0.WriteTo(mmio_space());

    return true;
}

} // namespace i915

// Modesetting functions

namespace {

// See the section on HDMI/DVI programming in intel-gfx-prm-osrc-skl-vol12-display.pdf
// for documentation on this algorithm.
static bool calculate_params(uint32_t symbol_clock_khz,
                             uint64_t* dco_freq_khz, uint32_t* dco_central_freq_khz,
                             uint8_t* p0, uint8_t* p1, uint8_t* p2) {
    uint8_t even_candidates[36] = {
        4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42,
        44, 48, 52, 54, 56, 60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90, 92, 96, 98
    };
    uint8_t odd_candidates[7] = { 3, 5, 7, 9, 15, 21, 35 };
    uint32_t candidate_freqs[3] = { 8400000, 9000000, 9600000 };
    uint32_t chosen_central_freq = 0;
    uint8_t chosen_divisor = 0;
    uint64_t afe_clock = symbol_clock_khz * 5;
    uint32_t best_deviation = 60; // Deviation in .1% intervals

    for (int parity = 0; parity < 2; parity++) {
        uint8_t* candidates;
        uint8_t num_candidates;
        if (parity) {
            candidates = odd_candidates;
            num_candidates = sizeof(odd_candidates) / sizeof(*odd_candidates);
        } else {
            candidates = even_candidates;
            num_candidates = sizeof(even_candidates) / sizeof(*even_candidates);
        }

        for (unsigned i = 0; i < sizeof(candidate_freqs) / sizeof(*candidate_freqs); i++) {
            uint32_t candidate_freq = candidate_freqs[i];
            for (unsigned j = 0; j < num_candidates; j++) {
                uint8_t candidate_divisor = candidates[j];
                uint64_t dco_freq = candidate_divisor * afe_clock;
                if (dco_freq > candidate_freq) {
                    uint32_t deviation = static_cast<uint32_t>(
                            1000 * (dco_freq - candidate_freq) / candidate_freq);
                    // positive deviation must be < 1%
                    if (deviation < 10 && deviation < best_deviation) {
                        best_deviation = deviation;
                        chosen_central_freq = candidate_freq;
                        chosen_divisor = candidate_divisor;
                    }
                } else {
                    uint32_t deviation = static_cast<uint32_t>(
                            1000 * (candidate_freq - dco_freq) / candidate_freq);
                    if (deviation < best_deviation) {
                        best_deviation = deviation;
                        chosen_central_freq = candidate_freq;
                        chosen_divisor = candidate_divisor;
                    }
                }
            }
        }
        if (chosen_divisor) {
            break;
        }
    }
    if (!chosen_divisor) {
        return false;
    }
    *p0 = *p1 = *p2 = 1;
    if (chosen_divisor % 2 == 0) {
        uint8_t chosen_divisor1 = chosen_divisor / 2;
        if (chosen_divisor1 == 1 || chosen_divisor1 == 2
                || chosen_divisor1 == 3 || chosen_divisor1 == 5) {
            *p0 = 2;
            *p2 = chosen_divisor1;
        } else if (chosen_divisor1 % 2 == 0) {
            *p0 = 2;
            *p1 = chosen_divisor1 / 2;
            *p2 = 2;
        } else if (chosen_divisor1 % 3 == 0) {
            *p0 = 3;
            *p1 = chosen_divisor1 / 3;
            *p2 = 2;
        } else if (chosen_divisor1 % 7 == 0) {
            *p0 = 7;
            *p1 = chosen_divisor1 / 7;
            *p2 = 2;
        }
    } else if (chosen_divisor == 3 || chosen_divisor == 9) {
        *p0 = 3;
        *p2 = chosen_divisor / 3;
    } else if (chosen_divisor == 5 || chosen_divisor == 7) {
        *p0 = chosen_divisor;
    } else if (chosen_divisor == 15) {
        *p0 = 3;
        *p2 = 5;
    } else if (chosen_divisor == 21) {
        *p0 = 7;
        *p2 = 3;
    } else if (chosen_divisor == 35) {
        *p0 = 7;
        *p2 = 5;
    }
    *dco_freq_khz = chosen_divisor * afe_clock;
    *dco_central_freq_khz = chosen_central_freq;
    return true;
}

} // namespace

namespace i915 {

bool HdmiDisplay::QueryDevice(edid::Edid* edid, zx_display_info* info) {
    // HDMI isn't supported on these DDIs
    if (ddi_to_pin(ddi()) == -1) {
        return false;
    }

    // Reset the GMBus registers and disable GMBus interrupts
    registers::GMBus0::Get().FromValue(0).WriteTo(mmio_space());
    registers::GMBus4::Get().FromValue(0).WriteTo(mmio_space());

    edid::timing_params_t timing_params;
    const char* edid_err;
    if (!edid->Init(this, &edid_err)) {
        zxlogf(TRACE, "i915: hdmi edid init failed \"%s\"\n", edid_err);
        return false;
    } else if (!edid->GetPreferredTiming(&timing_params)
            || !edid->CheckForHdmi(&is_hdmi_display_)) {
        zxlogf(TRACE, "i915: failed to find valid timing and hdmi\n");
        return false;
    }
    zxlogf(TRACE, "Found a %s monitor\n", is_hdmi_display_ ? "hdmi" : "dvi");

    info->width = timing_params.horizontal_addressable;
    info->height = timing_params.vertical_addressable;
    info->format = ZX_PIXEL_FORMAT_ARGB_8888;
    info->stride = registers::PlaneSurfaceStride::compute_linear_stride(info->width, info->format);
    info->pixelsize = ZX_PIXEL_FORMAT_BYTES(info->format);

    return true;
}

bool HdmiDisplay::DefaultModeset() {
    ResetPipe();
    if (!ResetTrans() || !ResetDdi()) {
        return false;
    }

    edid::timing_params_t timing_params;
    edid().GetPreferredTiming(&timing_params);

    registers::Dpll dpll = controller()->SelectDpll(false /* is_edp */, true /* is_hdmi */,
                                                    timing_params.pixel_freq_10khz);
    if (dpll == registers::DPLL_INVALID) {
        return false;
    }

    auto dpll_enable = registers::DpllEnable::Get(dpll).ReadFrom(mmio_space());
    if (!dpll_enable.enable_dpll()) {
        // Set the the DPLL control settings
        auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(mmio_space());
        dpll_ctrl1.dpll_hdmi_mode(dpll).set(1);
        dpll_ctrl1.dpll_override(dpll).set(1);
        dpll_ctrl1.dpll_ssc_enable(dpll).set(0);
        dpll_ctrl1.WriteTo(mmio_space());
        dpll_ctrl1.ReadFrom(mmio_space());

        // Calculate and the HDMI DPLL parameters
        uint8_t p0, p1, p2;
        uint32_t dco_central_freq_khz;
        uint64_t dco_freq_khz;
        if (!calculate_params(timing_params.pixel_freq_10khz * 10,
                              &dco_freq_khz, &dco_central_freq_khz, &p0, &p1, &p2)) {
            zxlogf(ERROR, "hdmi: failed to calculate clock params\n");
            return false;
        }

        // Set the DCO frequency
        auto dpll_cfg1 = registers::DpllConfig1::Get(dpll).FromValue(0);
        uint16_t dco_int = static_cast<uint16_t>((dco_freq_khz / 1000) / 24);
        uint16_t dco_frac = static_cast<uint16_t>(
                ((dco_freq_khz * (1 << 15) / 24) - ((dco_int * 1000L) * (1 << 15))) / 1000);
        dpll_cfg1.set_frequency_enable(1);
        dpll_cfg1.set_dco_integer(dco_int);
        dpll_cfg1.set_dco_fraction(dco_frac);
        dpll_cfg1.WriteTo(mmio_space());
        dpll_cfg1.ReadFrom(mmio_space());

        // Set the divisors and central frequency
        auto dpll_cfg2 = registers::DpllConfig2::Get(dpll).FromValue(0);
        dpll_cfg2.set_qdiv_ratio(p1);
        dpll_cfg2.set_qdiv_mode(p1 != 1);
        if (p2 == 5) {
            dpll_cfg2.set_kdiv_ratio(dpll_cfg2.kKdiv5);
        } else if (p2 == 2) {
            dpll_cfg2.set_kdiv_ratio(dpll_cfg2.kKdiv2);
        } else if (p2 == 3) {
            dpll_cfg2.set_kdiv_ratio(dpll_cfg2.kKdiv3);
        } else { // p2 == 1
            dpll_cfg2.set_kdiv_ratio(dpll_cfg2.kKdiv1);
        }
        if (p0 == 1) {
            dpll_cfg2.set_pdiv_ratio(dpll_cfg2.kPdiv1);
        } else if (p0 == 2) {
            dpll_cfg2.set_pdiv_ratio(dpll_cfg2.kPdiv2);
        } else if (p0 == 3) {
            dpll_cfg2.set_pdiv_ratio(dpll_cfg2.kPdiv3);
        } else { // p0 == 7
            dpll_cfg2.set_pdiv_ratio(dpll_cfg2.kPdiv7);
        }
        if (dco_central_freq_khz == 9600000) {
            dpll_cfg2.set_central_freq(dpll_cfg2.k9600Mhz);
        } else if (dco_central_freq_khz == 9000000) {
            dpll_cfg2.set_central_freq(dpll_cfg2.k9000Mhz);
        } else { // dco_central_freq == 8400000
            dpll_cfg2.set_central_freq(dpll_cfg2.k8400Mhz);
        }
        dpll_cfg2.WriteTo(mmio_space());
        dpll_cfg2.ReadFrom(mmio_space()); // Posting read

        // Enable and wait for the DPLL
        dpll_enable.set_enable_dpll(1);
        dpll_enable.WriteTo(mmio_space());
        if (!WAIT_ON_MS(registers::DpllStatus
                ::Get().ReadFrom(mmio_space()).dpll_lock(dpll).get(), 5)) {
            zxlogf(ERROR, "hdmi: DPLL failed to lock\n");
            return false;
        }
    }

    // Direct the DPLL to the DDI
    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
    dpll_ctrl2.ddi_select_override(ddi()).set(1);
    dpll_ctrl2.ddi_clock_off(ddi()).set(0);
    dpll_ctrl2.ddi_clock_select(ddi()).set(dpll);
    dpll_ctrl2.WriteTo(mmio_space());

    // Enable DDI IO power and wait for it
    auto pwc2 = registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    pwc2.ddi_io_power_request(ddi()).set(1);
    pwc2.WriteTo(mmio_space());
    if (!WAIT_ON_US(registers::PowerWellControl2
            ::Get().ReadFrom(mmio_space()).ddi_io_power_state(ddi()).get(), 20)) {
        zxlogf(ERROR, "hdmi: failed to enable IO power for ddi\n");
        return false;
    }

    registers::TranscoderRegs trans_regs(trans());

    // Configure Transcoder Clock Select
    auto trans_clk_sel = trans_regs.ClockSelect().ReadFrom(mmio_space());
    trans_clk_sel.set_trans_clock_select(ddi() + 1);
    trans_clk_sel.WriteTo(mmio_space());

    // Configure the transcoder
    uint32_t h_active = timing_params.horizontal_addressable - 1;
    uint32_t h_sync_start = h_active + timing_params.horizontal_front_porch;
    uint32_t h_sync_end = h_sync_start + timing_params.horizontal_sync_pulse;
    uint32_t h_total = h_sync_end + timing_params.horizontal_back_porch;

    uint32_t v_active = timing_params.vertical_addressable - 1;
    uint32_t v_sync_start = v_active + timing_params.vertical_front_porch;
    uint32_t v_sync_end = v_sync_start + timing_params.vertical_sync_pulse;
    uint32_t v_total = v_sync_end + timing_params.vertical_back_porch;

    auto h_total_reg = trans_regs.HTotal().FromValue(0);
    h_total_reg.set_count_total(h_total);
    h_total_reg.set_count_active(h_active);
    h_total_reg.WriteTo(mmio_space());
    auto v_total_reg = trans_regs.VTotal().FromValue(0);
    v_total_reg.set_count_total(v_total);
    v_total_reg.set_count_active(v_active);
    v_total_reg.WriteTo(mmio_space());

    auto h_sync_reg = trans_regs.HSync().FromValue(0);
    h_sync_reg.set_sync_start(h_sync_start);
    h_sync_reg.set_sync_end(h_sync_end);
    h_sync_reg.WriteTo(mmio_space());
    auto v_sync_reg = trans_regs.VSync().FromValue(0);
    v_sync_reg.set_sync_start(v_sync_start);
    v_sync_reg.set_sync_end(v_sync_end);
    v_sync_reg.WriteTo(mmio_space());

    // The Intel docs say that H/VBlank should be programmed with the same H/VTotal
    trans_regs.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(mmio_space());
    trans_regs.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(mmio_space());

    auto ddi_func = trans_regs.DdiFuncControl().ReadFrom(mmio_space());
    ddi_func.set_trans_ddi_function_enable(1);
    ddi_func.set_ddi_select(ddi());
    ddi_func.set_trans_ddi_mode_select(is_hdmi_display_ ? ddi_func.kModeHdmi : ddi_func.kModeDvi);
    ddi_func.set_bits_per_color(ddi_func.k8bbc);
    ddi_func.set_sync_polarity(timing_params.vertical_sync_polarity << 1
                                | timing_params.horizontal_sync_polarity);
    ddi_func.set_port_sync_mode_enable(0);
    ddi_func.set_dp_vc_payload_allocate(0);
    ddi_func.WriteTo(mmio_space());

    auto trans_conf = trans_regs.Conf().ReadFrom(mmio_space());
    trans_conf.set_transcoder_enable(1);
    trans_conf.set_interlaced_mode(timing_params.interlaced);
    trans_conf.WriteTo(mmio_space());

    // Configure voltage swing and related IO settings.
    registers::DdiRegs ddi_regs(ddi());
    auto ddi_buf_trans_hi = ddi_regs.DdiBufTransHi(9).ReadFrom(mmio_space());
    auto ddi_buf_trans_lo = ddi_regs.DdiBufTransLo(9).ReadFrom(mmio_space());
    auto disio_cr_tx_bmu = registers::DisplayIoCtrlRegTxBmu::Get().ReadFrom(mmio_space());

    // kUseDefaultIdx always fails the idx-in-bounds check, so no additional handling is needed
    uint8_t idx = controller()->igd_opregion().GetHdmiBufferTranslationIndex(ddi());
    uint8_t i_boost_override = controller()->igd_opregion().GetIBoost(ddi(), false /* is_dp */);

    const ddi_buf_trans_entry* entries;
    uint8_t default_iboost;
    if (is_skl_y(controller()->device_id()) || is_kbl_y(controller()->device_id())) {
        entries = hdmi_ddi_buf_trans_skl_y;
        if (idx >= fbl::count_of(hdmi_ddi_buf_trans_skl_y)) {
            idx = 8; // Default index
        }
        default_iboost = 3;
    } else {
        entries = hdmi_ddi_buf_trans_skl_uhs;
        if (idx >= fbl::count_of(hdmi_ddi_buf_trans_skl_uhs)) {
            idx = 8; // Default index
        }
        default_iboost = 1;
    }

    ddi_buf_trans_hi.set_reg_value(entries[idx].high_dword);
    ddi_buf_trans_lo.set_reg_value(entries[idx].low_dword);
    if (i_boost_override) {
        ddi_buf_trans_lo.set_balance_leg_enable(1);
    }
    disio_cr_tx_bmu.set_disable_balance_leg(0);
    disio_cr_tx_bmu.tx_balance_leg_select(ddi()).set(
            i_boost_override ? i_boost_override : default_iboost);

    ddi_buf_trans_hi.WriteTo(mmio_space());
    ddi_buf_trans_lo.WriteTo(mmio_space());
    disio_cr_tx_bmu.WriteTo(mmio_space());

    // Configure and enable DDI_BUF_CTL
    auto ddi_buf_ctl = ddi_regs.DdiBufControl().ReadFrom(mmio_space());
    ddi_buf_ctl.set_ddi_buffer_enable(1);
    ddi_buf_ctl.WriteTo(mmio_space());

    // Configure the pipe
    registers::PipeRegs pipe_regs(pipe());

    auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
    pipe_size.set_horizontal_source_size(h_active);
    pipe_size.set_vertical_source_size(v_active);
    pipe_size.WriteTo(mmio_space());

    auto plane_control = pipe_regs.PlaneControl().FromValue(0);
    plane_control.set_plane_enable(1);
    plane_control.set_source_pixel_format(plane_control.kFormatRgb8888);
    plane_control.set_tiled_surface(plane_control.kLinear);
    plane_control.WriteTo(mmio_space());

    auto plane_size = pipe_regs.PlaneSurfaceSize().FromValue(0);
    plane_size.set_width_minus_1(h_active);
    plane_size.set_height_minus_1(v_active);
    plane_size.WriteTo(mmio_space());

    return true;
}

} // namespace i915
