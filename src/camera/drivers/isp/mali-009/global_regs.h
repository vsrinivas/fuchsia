// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace camera {

// some consts relating to the size of configs:
// Size of the global config in 32bit words:
constexpr size_t kGlobalConfigSize = 64;

class Id_Api : public hwreg::RegisterBase<Id_Api, uint32_t> {
public:
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<Id_Api>(0x0);
    }
};

class Id_Product : public hwreg::RegisterBase<Id_Product, uint32_t> {
public:
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<Id_Product>(0x4);
    }
};

class Id_Version : public hwreg::RegisterBase<Id_Version, uint32_t> {
public:
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<Id_Version>(0x8);
    }
};

class Id_Revision : public hwreg::RegisterBase<Id_Revision, uint32_t> {
public:
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<Id_Revision>(0xc);
    }
};

class IspGlobal_Config0 : public hwreg::RegisterBase<IspGlobal_Config0, uint32_t> {
public:
    // [0]    : check bid
    // [1]    : check rid
    //   The following signals are diagnostic signals which helps
    //    identifying some of the AXI interface
    //  error cases. This must be used only as debug signals. You should
    //   follow these sequence
    // - Mask the dma error interrupt
    // - clear DMA interrupt if there is an active interrupt
    // - Write appropriate values to these registers
    // - Clear DMA alarms to clear the existing alarm
    // - unmask the DMA error interrpt
    //   [2]    : frame_write_cancel/frame_read_cancel
    // [10:3] : awmaxwait_limit/armaxwait_limit
    // [18:11]: wmaxwait_limit
    // [26:19]: waxct_ostand_limit/rxnfr_ostand_limit
    DEF_FIELD(30, 4, dma_global_config);
    //  1 = synchronous reset of FSMs in design (faster recovery after
    //       broken frame)
    //      when the MCU detects a broken frame or any other abnormal
    //       condition, the global_fsm_rest is
    //     expected to be used.
    //     MCU needs to follow a certain sequence for the same
    //          1. Read the status register to know the exact source of
    //            the error interrupt.
    //         2. Read the details of the error status register.
    //         3. Mask all the interrupts.
    //          4. Configure the input port register ISP_COMMON:input
    //            port: mode request to safe_stop mode.
    //          5. Read back the ISP_COMMON:input port: mode status
    //            register to see the status of mode request.
    //     And wait until it shows the correct status.
    //          6. Wait for the ISP_COMMON:isp global monitor: fr
    //            pipeline busy signal to become low.
    //         7. Assert the global fsm reset.
    //         8. Clear the global fsm reset.
    //         9. Reconfigure the ISP configuration space.
    //          10. Unmask the necessary interrupt sources. 11. Configure
    //             the input port in safe_start mode.
    DEF_BIT(0, global_fsm_reset);
    // 1 = synchronous reset of FSMs in scaler design
    DEF_BIT(1, scaler_fsm_reset);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_Config0>(0x10);
    }
};

class IspGlobal_Config1 : public hwreg::RegisterBase<IspGlobal_Config1, uint32_t> {
public:
    //  Horizontal blanking interval during regeneration (0=measured
    //   input interval)
    DEF_FIELD(15, 0, flush_hblank);
    //  0=linearised data after linearised cluster, MSB aligned.
    //     {data[19:0], 16'd0}, 1=static DPC output, MSB aligned.
    //     {data[15:0], 20'd0}, 2=Output of CA correction, MSB aligned.
    //     {data[15:0], 20'd0}, 3=CNR output, {B[11:0], G[11:0],
    //     R[11:0]}, 4=output forces to 0, 5=Reserved 5, 6=Reserved 6,
    //     7=Reserved 7
    DEF_FIELD(18, 16, isp_monitor_select);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_Config1>(0x14);
    }
};

class IspGlobal_Config2 : public hwreg::RegisterBase<IspGlobal_Config2, uint32_t> {
public:
    //  Minimun H-blank. The frame monitor will checke the frame geometry
    //   against this value
    DEF_FIELD(15, 0, interline_blanks_min);
    //  Minimun V-blank. The frame monitor will checke the frame geometry
    //   against this value
    DEF_FIELD(31, 16, interframe_blanks_min);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_Config2>(0x18);
    }
};

class IspGlobal_WatchdogTimerMaxCount :
      public hwreg::RegisterBase<IspGlobal_WatchdogTimerMaxCount, uint32_t> {
public:
    //  Max count after which watchdog timer should give an interrupt.
    //   this count is between frame start and frame end
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_WatchdogTimerMaxCount>(0x1c);
    }
};

class IspGlobal_Config3 : public hwreg::RegisterBase<IspGlobal_Config3, uint32_t> {
public:
    //  mcu override config select. When this bit is set, MCU takes
    //   control of the ISP ping-pong config swap.
    //      when is signal is set to 1, ISP core works in slave mode and
    //       selects configuration space based on MCU instruction.
    DEF_BIT(0, mcu_override_config_select);
    // This signal is valid when Mcu_override_config_select is set to 1.
    //          when mcu takes control of the config select, this signal
    //           indicated whether to use ping and pong config space
    //          If this signal is changed during active active frame, the
    //           hardware makes sure that the config space is changed
    //  in the next vertical blanking (0=Use pong address space, 1=Use
    //   Ping address space)
    DEF_BIT(1, mcu_ping_pong_config_select);
    //  Multi-context control mode (0=default mode, this is for single
    //   context, 1=multi-context mode)
    DEF_BIT(8, multi_context_mode);

    IspGlobal_Config3& select_config_ping() {
        set_mcu_ping_pong_config_select(1);
        return *this;
    }

    IspGlobal_Config3& select_config_pong() {
        set_mcu_ping_pong_config_select(0);
        return *this;
    }

    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_Config3>(0x20);
    }
};

class IspGlobal_Config4 : public hwreg::RegisterBase<IspGlobal_Config4, uint32_t> {
public:
    //  context swap status. when a address space is locked, all write to
    //   that address space will be rejected internally
    //        This signal is set when the 1st pixel comes out of input
    //         port and gets cleared when the last pixel comes out of ISp
    //         in streaming channels
    //     1: ping locked
    //     0: ping free
    DEF_BIT(0, ping_locked);
    //  context swap status. when a address space is locked, all write to
    //   that address space will be rejected internally.
    //        This signal is set when the 1st pixel comes out of input
    //         port and gets cleared when the last pixel comes out of ISp
    //         in streaming channels
    //     1: pong locked
    //     0: pong free
    DEF_BIT(1, pong_locked);
    //  this signal indicates which of the PING/PONG config is being used
    //   by ISP. when MCU takes control of the config management through
    //      Mcu_override_config_select signal, then this signal is just a
    //       reflection of what MCU has instructed through
    //       Mcu_ping_pong_config_select
    //     signal.
    //        This signal is a good point to synchronize with hardware.
    //         MCU should read this signal in a regular interval to
    //         synchronize with its
    //     internal state.
    //       This signal is changed when the 1st pixel comes in. So this
    //        signal must be sampled at the frame_start interrupt.
    //      0: pong in use by ISP
    //     1: ping in use by ISP
    DEF_BIT(2, ping_pong_config_select);

    bool is_ping() const {
        return ping_pong_config_select() == 1;
    }

    bool is_pong() const {
        return ping_pong_config_select() == 0;
    }

    static auto Get() {
        return hwreg::RegisterAddr<IspGlobal_Config4>(0x24);
    }
};

class IspGlobalMeteringBaseAddr :
      public hwreg::RegisterBase<IspGlobalMeteringBaseAddr, uint32_t> {
public:
    // base address for AWB stats. Value is set for 33x33 max zones
    DEF_FIELD(15, 0, awb);
    // base address for AF stats. Value is set for 33x33 max zones
    DEF_FIELD(31, 16, af);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMeteringBaseAddr>(0x28);
    }
};

class IspGlobalMeteringBaseAddr_MaxAddr :
      public hwreg::RegisterBase<IspGlobalMeteringBaseAddr_MaxAddr, uint32_t> {
public:
    // max address for metering stats mem. Value is set for 33x33 max zones
    DEF_FIELD(15, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMeteringBaseAddr_MaxAddr>(0x2c);
    }
};

class IspGlobalInterrupt_MaskVector :
      public hwreg::RegisterBase<IspGlobalInterrupt_MaskVector, uint32_t> {
public:
    DEF_BIT(0, isp_start);
    DEF_BIT(1, isp_done);
    DEF_BIT(2, ctx_management_error);
    DEF_BIT(3, broken_frame_error);
    DEF_BIT(4, metering_af_done);
    DEF_BIT(5, metering_aexp_done);
    DEF_BIT(6, metering_awb_done);
    DEF_BIT(7, metering_aexp_1024_bin_hist_done);
    DEF_BIT(8, metering_antifog_hist_done);
    DEF_BIT(9, lut_init_done);
    DEF_BIT(11, fr_y_dma_write_done);
    DEF_BIT(12, fr_uv_dma_write_done);
    DEF_BIT(13, ds_y_dma_write_done);
    DEF_BIT(14, linearization_done);
    DEF_BIT(15, static_dpc_done);
    DEF_BIT(16, ca_correction_done);
    DEF_BIT(17, iridix_done);
    DEF_BIT(18, three_d_liut_done);
    DEF_BIT(19, wdg_timer_timed_out);
    DEF_BIT(20, frame_collision_error);
    DEF_BIT(21, luma_variance_done);
    DEF_BIT(22, dma_error_interrupt);
    DEF_BIT(23, input_port_safely_stopped);

    IspGlobalInterrupt_MaskVector& mask_all() {
        set_isp_start(1);
        set_isp_done(1);
        set_ctx_management_error(1);
        set_broken_frame_error(1);
        set_metering_af_done(1);
        set_metering_aexp_done(1);
        set_metering_awb_done(1);
        set_metering_aexp_1024_bin_hist_done(1);
        set_metering_antifog_hist_done(1);
        set_lut_init_done(1);
        set_fr_y_dma_write_done(1);
        set_fr_uv_dma_write_done(1);
        set_ds_y_dma_write_done(1);
        set_linearization_done(1);
        set_static_dpc_done(1);
        set_ca_correction_done(1);
        set_iridix_done(1);
        set_three_d_liut_done(1);
        set_wdg_timer_timed_out(1);
        set_frame_collision_error(1);
        set_luma_variance_done(1);
        set_dma_error_interrupt(1);
        set_input_port_safely_stopped(1);
        return *this;
    }

    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_MaskVector>(0x30);
    }
};

class IspGlobalInterrupt_ClearVector :
      public hwreg::RegisterBase<IspGlobalInterrupt_ClearVector, uint32_t> {
public:
    DEF_BIT(0, isp_start);
    DEF_BIT(1, isp_done);
    DEF_BIT(2, ctx_management_error);
    DEF_BIT(3, broken_frame_error);
    DEF_BIT(4, metering_af_done);
    DEF_BIT(5, metering_aexp_done);
    DEF_BIT(6, metering_awb_done);
    DEF_BIT(7, metering_aexp_1024_bin_hist_done);
    DEF_BIT(8, metering_antifog_hist_done);
    DEF_BIT(9, lut_init_done);
    DEF_BIT(11, fr_y_dma_write_done);
    DEF_BIT(12, fr_uv_dma_write_done);
    DEF_BIT(13, ds_y_dma_write_done);
    DEF_BIT(14, linearization_done);
    DEF_BIT(15, static_dpc_done);
    DEF_BIT(16, ca_correction_done);
    DEF_BIT(17, iridix_done);
    DEF_BIT(18, three_d_liut_done);
    DEF_BIT(19, wdg_timer_timed_out);
    DEF_BIT(20, frame_collision_error);
    DEF_BIT(21, luma_variance_done);
    DEF_BIT(22, dma_error_interrupt);
    DEF_BIT(23, input_port_safely_stopped);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_ClearVector>(0x34);
    }
};

class IspGlobalInterrupt_ShadowDisableVector :
      public hwreg::RegisterBase<IspGlobalInterrupt_ShadowDisableVector, uint32_t> {
public:
    DEF_BIT(0, isp_start);
    DEF_BIT(1, isp_done);
    DEF_BIT(2, ctx_management_error);
    DEF_BIT(3, broken_frame_error);
    DEF_BIT(4, metering_af_done);
    DEF_BIT(5, metering_aexp_done);
    DEF_BIT(6, metering_awb_done);
    DEF_BIT(7, metering_aexp_1024_bin_hist_done);
    DEF_BIT(8, metering_antifog_hist_done);
    DEF_BIT(9, lut_init_done);
    DEF_BIT(11, fr_y_dma_write_done);
    DEF_BIT(12, fr_uv_dma_write_done);
    DEF_BIT(13, ds_y_dma_write_done);
    DEF_BIT(14, linearization_done);
    DEF_BIT(15, static_dpc_done);
    DEF_BIT(16, ca_correction_done);
    DEF_BIT(17, iridix_done);
    DEF_BIT(18, three_d_liut_done);
    DEF_BIT(19, wdg_timer_timed_out);
    DEF_BIT(20, frame_collision_error);
    DEF_BIT(21, luma_variance_done);
    DEF_BIT(22, dma_error_interrupt);
    DEF_BIT(23, input_port_safely_stopped);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_ShadowDisableVector>(0x38);
    }
};

class IspGlobalInterrupt_PulseMode :
      public hwreg::RegisterBase<IspGlobalInterrupt_PulseMode, uint32_t> {
public:
    //  When set to 1, the output interrupt will be a pulse. Otherwise it
    //   should be level.
    DEF_BIT(0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_PulseMode>(0x3c);
    }
};

class IspGlobalInterrupt_Clear :
      public hwreg::RegisterBase<IspGlobalInterrupt_Clear, uint32_t> {
public:
    //  Interrupt clear vector register qualifier. First the vector must
    //   be written. Then this bit must be set to 1 and then cleared
    DEF_BIT(0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_Clear>(0x40);
    }
};

class IspGlobalInterrupt_StatusVector :
      public hwreg::RegisterBase<IspGlobalInterrupt_StatusVector, uint32_t> {
public:
    DEF_BIT(0, isp_start);
    DEF_BIT(1, isp_done);
    DEF_BIT(2, ctx_management_error);
    DEF_BIT(3, broken_frame_error);
    DEF_BIT(4, metering_af_done);
    DEF_BIT(5, metering_aexp_done);
    DEF_BIT(6, metering_awb_done);
    DEF_BIT(7, metering_aexp_1024_bin_hist_done);
    DEF_BIT(8, metering_antifog_hist_done);
    DEF_BIT(9, lut_init_done);
    DEF_BIT(11, fr_y_dma_write_done);
    DEF_BIT(12, fr_uv_dma_write_done);
    DEF_BIT(13, ds_y_dma_write_done);
    DEF_BIT(14, linearization_done);
    DEF_BIT(15, static_dpc_done);
    DEF_BIT(16, ca_correction_done);
    DEF_BIT(17, iridix_done);
    DEF_BIT(18, three_d_liut_done);
    DEF_BIT(19, wdg_timer_timed_out);
    DEF_BIT(20, frame_collision_error);
    DEF_BIT(21, luma_variance_done);
    DEF_BIT(22, dma_error_interrupt);
    DEF_BIT(23, input_port_safely_stopped);

    bool has_errors() const {
        return (broken_frame_error() || frame_collision_error() || dma_error_interrupt() ||
                ctx_management_error() || wdg_timer_timed_out());
    }

    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalInterrupt_StatusVector>(0x44);
    }
};

class IspGlobalLp_ClockDisable :
      public hwreg::RegisterBase<IspGlobalLp_ClockDisable, uint32_t> {
public:
    //  When set, the output Y/RGB/YUV DMA writer in FR channel will be
    //   clock gated. This is applicable
    //      to Y channel of the semi-planner mode and all other
    //       non-planner modes. This must be used only when the DMA
    //       writer is not used and SOC
    //     environment uses the streaming outputs from ISP
    DEF_BIT(0, clk_dis_fr_y_dma_writer);
    //  When set, the output UV DMA writer in FR channel will be clock
    //   gated. This is applicable
    //      to UV channel of the semi-planner mode . This must be used
    //       only when the DMA writer is not used and SOC
    //      environment uses the streaming outputs from ISP, or format is
    //       NOT semi-planner
    DEF_BIT(1, clk_dis_fr_uv_dma_writer);
    //  When set, the output Y/RGB/YUV DMA writer in DS channel will be
    //   clock gated. This is applicable
    //      to Y channel of the semi-planner mode and all other
    //       non-planner modes. This must be used only when the DMA
    //       writer is not used and SOC
    //     environment uses the streaming outputs from ISP
    DEF_BIT(2, clk_dis_ds_y_dma_writer);
    //  When set, the output UV DMA writer in DS channel will be clock
    //   gated. This is applicable
    //      to UV channel of the semi-planner mode . This must be used
    //       only when the DMA writer is not used and SOC
    //      environment uses the streaming outputs from ISP, or format is
    //       NOT semi-planner
    DEF_BIT(3, clk_dis_ds_uv_dma_writer);
    //  This signal gates the clock for all temper write and read dma.
    //   This should be used only when Temper is not used.
    DEF_BIT(4, clk_dis_temper_dma);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalLp_ClockDisable>(0x48);
    }
};

class IspGlobalLp_ClockGateDisable :
      public hwreg::RegisterBase<IspGlobalLp_ClockGateDisable, uint32_t> {
public:
    // when set, this will disable V-blank Clock gating for WDR path channels.
    DEF_BIT(0, cg_dis_frame_stitch);
    // when set, this will disable V-blank Clock gating for raw frontend.
    DEF_BIT(1, cg_dis_raw_frontend);
    // when set, this will disable V-blank Clock gating for defect pixel.
    DEF_BIT(2, cg_dis_defect_pixel);
    // when set, this will disable V-blank Clock gating for Sinter.
    DEF_BIT(3, cg_dis_sinter);
    // when set, this will disable V-blank Clock gating for Temper.
    DEF_BIT(4, cg_dis_temper);
    // when set, this will disable V-blank Clock gating for CA correction.
    DEF_BIT(5, cg_dis_ca_correction);
    // when set, this will disable V-blank Clock gating for radial shading.
    DEF_BIT(6, cg_dis_radial_shading);
    // when set, this will disable V-blank Clock gating for mesh shading.
    DEF_BIT(7, cg_dis_mesh_shading);
    // when set, this will disable V-blank Clock gating for Iridix.
    DEF_BIT(8, cg_dis_iridix);
    // when set, this will disable V-blank Clock gating for Demosaic RGGB.
    DEF_BIT(9, cg_dis_demosaic_rggb);
    // when set, this will disable V-blank Clock gating for Demosaic RGBIr.
    DEF_BIT(10, cg_dis_demosaic_rgbir);
    // when set, this will disable V-blank Clock gating for PF correction.
    DEF_BIT(11, cg_dis_pf_correction);
    //  when set, this will disable V-blank Clock gating for CNR and
    //   pre-square root and post-square.
    DEF_BIT(12, cg_dis_cnr);
    // when set, this will disable V-blank Clock gating for 3D LUT.
    DEF_BIT(13, cg_dis_3d_lut);
    // when set, this will disable V-blank Clock gating for RGB scaler.
    DEF_BIT(14, cg_dis_rgb_scaler);
    //  when set, this will disable V-blank Clock gating for RGB gamma in
    //   FR pipeline.
    DEF_BIT(15, cg_dis_rgb_gamma_fr);
    //  when set, this will disable V-blank Clock gating for RGB gamma in
    //   DS pipeline.
    DEF_BIT(16, cg_dis_rgb_gamma_ds);
    //  when set, this will disable V-blank Clock gating for sharpen in
    //   FR pipeline.
    DEF_BIT(17, cg_dis_sharpen_fr);
    //  when set, this will disable V-blank Clock gating for sharpen in
    //   DS pipeline.
    DEF_BIT(18, cg_dis_sharpen_ds);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalLp_ClockGateDisable>(0x4c);
    }
};

class IspGlobalMonitor_Status :
      public hwreg::RegisterBase<IspGlobalMonitor_Status, uint32_t> {
public:
    // bit[0] : active width mismatch
    //     bit[1] : active_height mismatch
    //     bit[2] : minimum v-blank violated
    //     bit[3] : minimum h-blank violated
    DEF_FIELD(3, 0, broken_frame_status);
    //  This signal indicates if ISP pipeline is busy or Not, This signal
    //   doesnt include the metereing modules.
    //      This information is expected to be used when a broken frame
    //       is detected and global_fsm_reset needs to be asserted
    //     global_fsm_reset must be set when this busy signal is low.
    //         0: ISP main pipeline (excluding metering) is free
    //         1: ISP main pipeline (excluding metering) is busy
    DEF_BIT(16, fr_pipeline_busy);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMonitor_Status>(0x50);
    }
};

class IspGlobalMonitor_Failures :
      public hwreg::RegisterBase<IspGlobalMonitor_Failures, uint32_t> {
public:
    // [0] : temper_dma_lsb_wtr_axi_alarm
    //     [1] : temper_dma_lsb_rdr_axi_alarm
    //     [2] : temper_dma_msb_wtr_axi_alarm
    //     [3] : temper_dma_msb_rdr_axi_alarm
    //     [4] : FR UV dma axi alarm
    //     [5] : FR dma axi alarm
    //     [6] : DS UV dma axi alarm
    //     [7] : DS dma axi alarm
    //     [8] : Temper LSB dma frame dropped
    //     [9] : Temper MSB dma frame dropped
    //     [10]: FR UV-DMA frame dropped
    //     [11]: FR Y-DMA frame dropped
    //     [12]: DS UV-DMA frame dropped
    //     [13]: DS Y-DMA frame dropped
    DEF_FIELD(29, 16, dma_alarms);
    DEF_BIT(0, fr_y_dma_wfifo_fail_full);
    DEF_BIT(1, fr_y_dma_wfifo_fail_empty);
    DEF_BIT(2, fr_uv_dma_wfifo_fail_full);
    DEF_BIT(3, fr_uv_dma_wfifo_fail_empty);
    DEF_BIT(4, ds_y_dma_wfifo_fail_full);
    DEF_BIT(5, ds_y_dma_wfifo_fail_empty);
    DEF_BIT(6, ds_uv_dma_wfifo_fail_full);
    DEF_BIT(7, ds_uv_dma_wfifo_fail_empty);
    DEF_BIT(8, temper_dma_wfifo_fail_empty);
    DEF_BIT(9, temper_dma_wfifo_fail_full);
    DEF_BIT(10, temper_dma_rfifo_fail_empty);
    DEF_BIT(11, temper_dma_rfifo_fail_full);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMonitor_Failures>(0x54);
    }
};

class IspGlobalMonitor_ClearError :
      public hwreg::RegisterBase<IspGlobalMonitor_ClearError, uint32_t> {
public:
    // This signal must be asserted when the MCu gets broken frame interrupt.
    //      this signal must be 0->1->0 pulse. The duration of the pulse
    //       is not relevant. This rising edge will clear the
    //     broken frame error status signal
    DEF_BIT(0, broken_frame_error_clear);
    //  This signal must be asserted when the MCU gets the context error
    //   interrupt.
    //      this signal must be 0->1->0 pulse. The duration of the pulse
    //       is not relevant. This rising edge will clear the
    //     context error status signal
    DEF_BIT(1, context_error_clr);
    // This signal must be asserted when the MCU gets the DMA error interrupt.
    //      This signal will clear all DMA error status signals for all
    //       the output DMA writers (Y/UV DMAs in both of the output
    //       channels)
    //     MCU must follow the following sequance to clear the alarms
    //     step-1: set this bit to 1
    //     step-2: Read back the alarm signals
    //      step-3: If the alarms are cleared, then clear the clr_alarm
    //       signal back to 0.
    DEF_BIT(2, output_dma_clr_alarm);
    // This signal must be asserted when the MCU gets the DMA error interrupt.
    //      This signal will clear all DMA error status signals for all
    //       the Temper DMA writers/readers
    //     MCU must follow the following sequance to clear the alarms
    //     step-1: set this bit to 1
    //     step-2: Read back the alarm signals
    DEF_BIT(3, temper_dma_clr_alarm);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMonitor_ClearError>(0x58);
    }
};

class IspGlobalMonitor_MaxAddressDelayLine :
      public hwreg::RegisterBase<IspGlobalMonitor_MaxAddressDelayLine, uint32_t> {
public:
    // Delay line max address value for the full resolution ISP set outside ISP
    DEF_FIELD(15, 0, max_address_delay_line_fr);
    // Delay line max address value for the DS pipeline set outside ISP
    DEF_FIELD(31, 16, max_address_delay_line_ds);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalMonitor_MaxAddressDelayLine>(0x60);
    }
};

class IspGlobalChickenBit : public hwreg::RegisterBase<IspGlobalChickenBit, uint32_t> {
public:
    //  0: Only ISP main pipeline, ds pipeline and iridix filtering is
    //      used to generate the frame_done interrupt
    //    None of the metering done signals are considered here
    //  1: all metering done is taken into account to generate the frame
    //      done interrupt
    DEF_BIT(0, frame_end_select);
    // 0: temper dma reader will start reading based on the frame start
    //  1: temper dma reader will start reading based on linetick of the
    //      dma reader
    DEF_BIT(1, rd_start_sel);
    // 0=LSB aligned
    //  1=MSB aligned
    DEF_BIT(2, input_alignment);
    // 0=Watch dog timer enabled
    // 1=watch dog timer disabled
    DEF_BIT(3, watchdog_timer_dis);
    //  0 = When this chicken bit is et to 0, the soft reset (SW or HW
    //       generated) will be stretched by 1024 cycles
    //      so that it is asserted till the the pipeline delay of
    //       internal acl signals.
    //  1 = When thiss set to 1, the soft reset will not be stretched
    //       internally. The SW must make sure its
    //     kept high enough.
    DEF_BIT(4, soft_rst_apply_immediately);
    //  0=timeout enabled. At the end of the frame, if the last data is
    //     not drained out from DMA writer within 4000
    //    AXI clock, cycle, DMA will flush the FIFO and ignore the
    //     remainign data.
    //   1=timeout is disabled. If the last data is not drained out and
    //      the next frame starts coming in, DMA will drop the next frame
    //      and
    //   give an interrupt.
    //    If the timeout is disabled, its S/W responsibility to cancel
    //     the frame in all dma engines if frame drop interrupt comes.
    DEF_BIT(5, dma_writer_timeout_disable);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalChickenBit>(0x64);
    }
};

class IspGlobalParameterStatus :
      public hwreg::RegisterBase<IspGlobalParameterStatus, uint32_t> {
public:
    // 0: Demosaic RGBIr is present in the design
    //     1: Demodaic RGBIr is statically removed from the ISP.
    //          S/W must not access the ISP_CONFIG_PING:demosaic rgbir
    //           and the ISP_CONFIG_PONG:demosaic rgbir registers
    DEF_BIT(0, dmsc_rgbir);
    // 0: CA Correction is present in the design
    //     1: CA Correction is statically removed from the ISP.
    //  S/W must not access the ISP_CONFIG_PING:ca correction and the
    //   ISP_CONFIG_PONG:ca correction registers
    // Also SW must not access the following memories
    //    - CA_CORRECTION_FILTER_PING_MEM
    //    - CA_CORRECTION_FILTER_PONG_MEM
    //    - CA_CORRECTION_MESH_PING_MEM
    //    - CA_CORRECTION_MESH_PONG_MEM
    DEF_BIT(1, cac);
    // 0: DS pipeline is present in the design
    //     1: DS pipeline is statically removed from ISP.
    // S/W must not access the following register spaces
    //         - ISP_CONFIG_PING:ds crop and ISP_CONFIG_PONG:ds crop
    //         - ISP_CONFIG_PING:ds scaler and ISP_CONFIG_PONG:ds scaler
    //         - ISP_CONFIG_PING:ds gamma rgb and ISP_CONFIG_PONG:ds gamma rgb
    //         - ISP_CONFIG_PING:ds sharpen and ISP_CONFIG_PONG:ds sharpen
    //         - ISP_CONFIG_PING:ds cs conv and ISP_CONFIG_PONG:ds cs conv
    //         - ISP_CONFIG_PING:ds dma writer and ISP_CONFIG_PONG:ds dma writer
    //          - ISP_CONFIG_PING:ds uv dma writer and ISP_CONFIG_PONG:ds
    //             uv dma writer
    //  Also the S/W must not access the following memories
    //         - DS_SCALER_HFILT_COEFMEM
    //         - DS_SCALER_VFILT_COEFMEM
    //         - DS_GAMMA_RGB_PING_MEM
    //         - DS_GAMMA_RGB_PONG_MEM
    DEF_BIT(2, ds_pipe);
    // 0: sRGB gamma and 3D LUT is present in the design
    //     1: sRGB gamma and 3D LUT is statically removed from ISP.
    // The S/W must not accees the following register spaces
    //          -   ISP_CONFIG_PING:nonequ gamma and ISP_CONFIG_PING:
    //               nonequ gamma and
    // Also, the S/W must not access the following memories
    //         -   LUT3D_MEM
    DEF_BIT(3, lut_3d);
    // 0: SINTER2.5 used
    //     1: SINTER3 used
    DEF_BIT(4, sinter_version);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalParameterStatus>(0x68);
    }
};

class IspGlobalDbg : public hwreg::RegisterBase<IspGlobalDbg, uint32_t> {
public:
    // 0: debug signals are disabled
    //     1: debug signals are valid
    DEF_BIT(0, mode_en);
    // must be 0->1->0 to clear the debug counters
    DEF_BIT(8, clear_frame_cnt);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalDbg>(0x6c);
    }
};

class IspGlobalDbg_FrameCntCtx0 :
      public hwreg::RegisterBase<IspGlobalDbg_FrameCntCtx0, uint32_t> {
public:
    //  when debug mode is enabled, this register will show the frame
    //   count in context-0
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalDbg_FrameCntCtx0>(0x70);
    }
};

class IspGlobalDbg_FrameCntCtx1 :
      public hwreg::RegisterBase<IspGlobalDbg_FrameCntCtx1, uint32_t> {
public:
    //  when debug mode is enabled, this register will show the frame
    //   count in context-1
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalDbg_FrameCntCtx1>(0x74);
    }
};

class IspGlobalDbg_FrameCntCtx2 :
      public hwreg::RegisterBase<IspGlobalDbg_FrameCntCtx2, uint32_t> {
public:
    //  when debug mode is enabled, this register will show the frame
    //   count in context-2
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalDbg_FrameCntCtx2>(0x78);
    }
};

class IspGlobalDbg_FrameCntCtx3 :
      public hwreg::RegisterBase<IspGlobalDbg_FrameCntCtx3, uint32_t> {
public:
    //  when debug mode is enabled, this register will show the frame
    //   count in context-3
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<IspGlobalDbg_FrameCntCtx3>(0x7c);
    }
};

class InputPort_Config0 : public hwreg::RegisterBase<InputPort_Config0, uint32_t> {
public:
    //  Allows selection of various input port presets for standard
    //   sensor inputs.  See ISP Guide for details of available presets.
    //         2: preset mode 2, check ISP guide for details
    // 6: preset mode 6, check ISP guide for details        others: reserved
    DEF_FIELD(3, 0, preset);
    //  0=use vsync_i port for vertical sync, 1=use field_i port for
    //     vertical sync
    DEF_BIT(8, vs_use_field);
    // 0=vsync is pulse-type, 1=vsync is toggle-type (field signal)
    DEF_BIT(9, vs_toggle);
    //  0=horizontal counter reset on rising edge, 1=horizontal counter
    //     reset on falling edge
    DEF_BIT(10, vs_polarity);
    // 0=don't invert polarity for ACL gate, 1=invert polarity for ACL gate
    DEF_BIT(11, vs_polarity_acl);
    // 0=use hsync_i port for active-line, 1=use acl_i port for active-line
    DEF_BIT(12, hs_use_acl);
    //  0=vertical counter counts on hs, 1=vertical counter counts on
    //     horizontal counter overflow or reset
    DEF_BIT(14, vc_c_select);
    //  0=vertical counter is reset on edge of vs, 1=vertical counter is
    //     reset after timeout on hs
    DEF_BIT(15, vc_r_select);
    // 0=normal mode, 1=hvalid = hsync XOR vsync
    DEF_BIT(16, hs_xor_vs);
    //  0=don't invert polarity of HS for ACL gate, 1=invert polarity of
    //     HS for ACL gate
    DEF_BIT(17, hs_polarity);
    //  0=don't invert polarity of HS for HS gate, 1=invert polarity of
    //     HS for HS gate
    DEF_BIT(18, hs_polarity_acl);
    //  0=horizontal counter is reset on rising edge of hs, 1=horizontal
    //     counter is reset on vsync (e.g. when hsync is not available)
    DEF_BIT(19, hs_polarity_hs);
    //  0=vertical counter increments on rising edge of HS, 1=vertical
    //     counter increments on falling edge of HS
    DEF_BIT(20, hs_polarity_vc);
    //  0=vertical counter is reset on rising edge of hs, 1=vertical
    //     counter is reset on rising edge of vs
    DEF_BIT(23, hc_r_select);
    // 0=don't invert acl_i for acl gate, 1=invert acl_i for acl gate
    DEF_BIT(24, acl_polarity);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_Config0>(0x80);
    }
};

class InputPort_Config1 : public hwreg::RegisterBase<InputPort_Config1, uint32_t> {
public:
    // 0=don't invert field_i for field gate, 1=invert field_i for field gate
    DEF_BIT(0, field_polarity);
    // 0=field is pulse-type, 1=field is toggle-type
    DEF_BIT(1, field_toggle);
    //  0=exclude window0 signal in ACL gate, 1=include window0 signal in
    //     ACL gate
    DEF_BIT(8, aclg_window0);
    // 0=exclude hsync signal in ACL gate, 1=include hsync signal in ACL gate
    DEF_BIT(9, aclg_hsync);
    //  0=exclude window2 signal in ACL gate, 1=include window2 signal in
    //     ACL gate
    DEF_BIT(10, aclg_window2);
    // 0=exclude acl_i signal in ACL gate, 1=include acl_i signal in ACL gate
    DEF_BIT(11, aclg_acl);
    // 0=exclude vsync signal in ACL gate, 1=include vsync signal in ACL gate
    DEF_BIT(12, aclg_vsync);
    // 0=exclude window1 signal in HS gate, 1=include window1 signal in HS gate
    DEF_BIT(16, hsg_window1);
    // 0=exclude hsync signal in HS gate, 1=include hsync signal in HS gate
    DEF_BIT(17, hsg_hsync);
    // 0=exclude vsync signal in HS gate, 1=include vsync signal in HS gate
    DEF_BIT(18, hsg_vsync);
    //  0=exclude window2 signal in HS gate, 1=include window2 signal in
    //     HS gate (mask out spurious hs during blank)
    DEF_BIT(19, hsg_window2);
    //  0=exclude vsync signal in Field gate, 1=include vsync signal in
    //     Field gate
    DEF_BIT(24, fieldg_vsync);
    //  0=exclude window2 signal in Field gate, 1=include window2 signal
    //     in Field gate
    DEF_BIT(25, fieldg_window2);
    //  0=exclude field_i signal in Field gate, 1=include field_i signal
    //     in Field gate
    DEF_BIT(26, fieldg_field);
    // 0=pulse field, 1=toggle field
    DEF_BIT(27, field_mode);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_Config1>(0x84);
    }
};

class InputPort_HorizontalCrop0 :
      public hwreg::RegisterBase<InputPort_HorizontalCrop0, uint32_t> {
public:
    //  horizontal counter limit value (counts:
    //   0,1,...hc_limit-1,hc_limit,0,1,...)
    DEF_FIELD(15, 0, hc_limit);
    // window0 start for ACL gate.  See ISP guide for further details.
    DEF_FIELD(31, 16, hc_start0);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_HorizontalCrop0>(0x88);
    }
};

class InputPort_HorizontalCrop1 :
      public hwreg::RegisterBase<InputPort_HorizontalCrop1, uint32_t> {
public:
    // window0 size for ACL gate.  See ISP guide for further details.
    DEF_FIELD(15, 0, hc_size0);
    // window1 start for HS gate.  See ISP guide for further details.
    DEF_FIELD(31, 16, hc_start1);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_HorizontalCrop1>(0x8c);
    }
};

class InputPort_VerticalCrop0 :
      public hwreg::RegisterBase<InputPort_VerticalCrop0, uint32_t> {
public:
    // window1 size for HS gate.  See ISP guide for further details.
    DEF_FIELD(15, 0, hc_size1);
    // vertical counter limit value (counts: 0,1,...vc_limit-1,vc_limit,0,1,...)
    DEF_FIELD(31, 16, vc_limit);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_VerticalCrop0>(0x90);
    }
};

class InputPort_VerticalCrop1 :
      public hwreg::RegisterBase<InputPort_VerticalCrop1, uint32_t> {
public:
    // window2 start for ACL gate.  See ISP guide for further details.
    DEF_FIELD(15, 0, vc_start);
    // window2 size for ACL gate.  See ISP guide for further details.
    DEF_FIELD(31, 16, vc_size);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_VerticalCrop1>(0x94);
    }
};

class InputPort_FrameDim : public hwreg::RegisterBase<InputPort_FrameDim, uint32_t> {
public:
    // detected frame width.  Read only value.
    DEF_FIELD(15, 0, frame_width);
    // detected frame height.  Read only value.
    DEF_FIELD(31, 16, frame_height);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_FrameDim>(0x98);
    }
};

class InputPort_Config3 : public hwreg::RegisterBase<InputPort_Config3, uint32_t> {
public:
    // Used to stop and start input port.  See ISP guide for further details.
    //   Only modes-0 and 1 are used. all other values are reserved
    //    (0=safe stop, 1=safe start, 2=Reserved 2, 3=Reserved 3,
    //    4=Reserved 4, 5=Reserved 5, 6=Reserved 6, 7=Reserved 7)
    DEF_FIELD(2, 0, mode_request);
    //  Used to freeze input port configuration.  Used when multiple
    //   register writes are required to change input port configuration.
    //   (0=normal operation, 1=hold previous input port config state)
    DEF_BIT(7, freeze_config);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_Config3>(0x9c);
    }
};

class InputPort_ModeStatus : public hwreg::RegisterBase<InputPort_ModeStatus, uint32_t> {
public:
    //  Used to monitor input port status:       bit 0: 1=running,
    //   0=stopped, bits 1,2-reserved
    DEF_FIELD(2, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPort_ModeStatus>(0xa0);
    }
};

class InputPortFrameStats_StatsReset :
      public hwreg::RegisterBase<InputPortFrameStats_StatsReset, uint32_t> {
public:
    //  Resets the frame statistics registers and starts the sampling
    //   period. Note all the frame statistics saturate at 2^31
    DEF_BIT(0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_StatsReset>(0xa4);
    }
};

class InputPortFrameStats_StatsHold :
      public hwreg::RegisterBase<InputPortFrameStats_StatsHold, uint32_t> {
public:
    //  Synchronously disables the update of the statistics registers.
    //   This should be used prior to reading out the register values so
    //   as to ensure consistent values
    DEF_BIT(0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_StatsHold>(0xa8);
    }
};

class InputPortFrameStats_ActiveWidthMin :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveWidthMin, uint32_t> {
public:
    //  The minimum number of active pixels observed in a line within the
    //   sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveWidthMin>(0xb4);
    }
};

class InputPortFrameStats_ActiveWidthMax :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveWidthMax, uint32_t> {
public:
    //  The maximum number of active pixels observed in a line within the
    //   sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveWidthMax>(0xb8);
    }
};

class InputPortFrameStats_ActiveWidthSum :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveWidthSum, uint32_t> {
public:
    //  The total number of the active pixels values observed in a line
    //   within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveWidthSum>(0xbc);
    }
};

class InputPortFrameStats_ActiveWidthNum :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveWidthNum, uint32_t> {
public:
    // The number of lines observed within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveWidthNum>(0xc0);
    }
};

class InputPortFrameStats_ActiveHeightMin :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveHeightMin, uint32_t> {
public:
    //  The minimum number of active lines in the frames observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveHeightMin>(0xc4);
    }
};

class InputPortFrameStats_ActiveHeightMax :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveHeightMax, uint32_t> {
public:
    //  The maximum number of active lines in the frames observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveHeightMax>(0xc8);
    }
};

class InputPortFrameStats_ActiveHeightSum :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveHeightSum, uint32_t> {
public:
    //  The total number of active lines in the frames observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveHeightSum>(0xcc);
    }
};

class InputPortFrameStats_ActiveHeightNum :
      public hwreg::RegisterBase<InputPortFrameStats_ActiveHeightNum, uint32_t> {
public:
    // The total number of frames observed within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_ActiveHeightNum>(0xd0);
    }
};

class InputPortFrameStats_HblankMin :
      public hwreg::RegisterBase<InputPortFrameStats_HblankMin, uint32_t> {
public:
    //  The minimum number of horizontal blanking samples observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_HblankMin>(0xd4);
    }
};

class InputPortFrameStats_HblankMax :
      public hwreg::RegisterBase<InputPortFrameStats_HblankMax, uint32_t> {
public:
    //  The maximum number of horizontal blanking samples observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_HblankMax>(0xd8);
    }
};

class InputPortFrameStats_HblankSum :
      public hwreg::RegisterBase<InputPortFrameStats_HblankSum, uint32_t> {
public:
    //  The total number of the horizontal blanking samples observed
    //   within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_HblankSum>(0xdc);
    }
};

class InputPortFrameStats_HblankNum :
      public hwreg::RegisterBase<InputPortFrameStats_HblankNum, uint32_t> {
public:
    // The total number of the lines observed within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_HblankNum>(0xe0);
    }
};

class InputPortFrameStats_VblankMin :
      public hwreg::RegisterBase<InputPortFrameStats_VblankMin, uint32_t> {
public:
    //  The minimum number of vertical blanking samples observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_VblankMin>(0xe4);
    }
};

class InputPortFrameStats_VblankMax :
      public hwreg::RegisterBase<InputPortFrameStats_VblankMax, uint32_t> {
public:
    //  The maximum number of vertical blanking samples observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_VblankMax>(0xe8);
    }
};

class InputPortFrameStats_VblankSum :
      public hwreg::RegisterBase<InputPortFrameStats_VblankSum, uint32_t> {
public:
    //  The total number of the vertical blanking samples observed within
    //   the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_VblankSum>(0xec);
    }
};

class InputPortFrameStats_VblankNum :
      public hwreg::RegisterBase<InputPortFrameStats_VblankNum, uint32_t> {
public:
    // The total number of frames observed within the sampling period
    DEF_FIELD(31, 0, value);
    static auto Get() {
        return hwreg::RegisterAddr<InputPortFrameStats_VblankNum>(0xf0);
    }
};

} // namespace camera
