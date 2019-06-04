// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <lib/mmio/mmio.h>
#include <zircon/types.h>

namespace camera {

// ISP memory offsets
constexpr uint32_t kDecompander0PingOffset = 0xAB6C;
constexpr uint32_t kPingConfigSize = 0x17FC0;
constexpr uint32_t kAexpHistStatsOffset = 0x24A8;
constexpr uint32_t kHistSize = 0x2000;
constexpr uint32_t kPingMeteringStatsOffset = 0x44B0;
constexpr uint32_t kPongMeteringStatsOffset = kPingMeteringStatsOffset + kPingConfigSize;
constexpr uint32_t kDecompander0PongOffset = kDecompander0PingOffset + kPingConfigSize;
constexpr uint32_t kMeteringSize = 0x8000;
constexpr uint32_t kLocalBufferSize = (0x18e88 + 0x4000);
constexpr uint32_t kConfigSize = 0x1231C;
constexpr uint32_t kPingContextConfigOffset = 0x18e88;
constexpr uint32_t kPongContextConfigOffset = 0x30E48;
constexpr uint32_t kContextConfigSize = 0x1000; // in 32 bit words

#define DEF_NAMESPACE_REG(classname, namespc, address)                                \
    namespace namespc {                                                               \
    class classname : public camera::classname {                                      \
    public:                                                                           \
        static auto Get() { return hwreg::RegisterAddr<camera::classname>(address); } \
    };                                                                                \
    }

class Top_ActiveDim : public hwreg::RegisterBase<Top_ActiveDim, uint32_t> {
public:
    // Active video width in pixels
    DEF_FIELD(15, 0, active_width);
    // Active video height in lines
    DEF_FIELD(31, 16, active_height);
};

DEF_NAMESPACE_REG(Top_ActiveDim, ping, 0x18e88)
DEF_NAMESPACE_REG(Top_ActiveDim, pong, 0x30e48)

class Top_Config : public hwreg::RegisterBase<Top_Config, uint32_t> {
public:
    //  Starting color of the rggb pattern for all the modules before
    //   mirror (0=R Gr   Gb B, 1=Gr R   B Gb, 2=Gb B   R Gr, 3=B Gb   Gr
    //   R)
    DEF_FIELD(1, 0, rggb_start_pre_mirror);
    // Starting color of the rggb pattern for all the modules after mirror
    // this must be same as RGGB start pre mirror if mirror is bypassed
    DEF_FIELD(9, 8, rggb_start_post_mirror);
    //  The pixel arrangement of the CFA array on the sensor. Set in the
    //   Top register group and used by several blocks in the pipeline:
    //   (0=RGGB, 1=reserved, 2=RIrGB, 3=RGIrB)
    DEF_FIELD(17, 16, cfa_pattern);
    //  Linear data src (0=Sensor stitched and linear data directly
    //   coming from sensor, 1=linear data from frame stitch, 2=Sensor
    //   companded data linearised through decompander, 3=reserved)
    DEF_FIELD(25, 24, linear_data_src);
};

DEF_NAMESPACE_REG(Top_Config, ping, 0x18e8c)
DEF_NAMESPACE_REG(Top_Config, pong, 0x30e4c)

class Top_Bypass0 : public hwreg::RegisterBase<Top_Bypass0, uint32_t> {
public:
    // Bypass video test generator
    DEF_BIT(0, bypass_video_test_gen);
    // Bypass input formatter module.
    DEF_BIT(1, bypass_input_formatter);
    // Bypass front end decompander
    DEF_BIT(2, bypass_decompander);
    // Bypass sensor offset wdr
    DEF_BIT(3, bypass_sensor_offset_wdr);
    // Bypass gain wdr
    DEF_BIT(4, bypass_gain_wdr);
    // Bypass frame stitching logic
    DEF_BIT(5, bypass_frame_stitch);
};

DEF_NAMESPACE_REG(Top_Bypass0, ping, 0x18eac)
DEF_NAMESPACE_REG(Top_Bypass0, pong, 0x30e6c)

class Top_Bypass1 : public hwreg::RegisterBase<Top_Bypass1, uint32_t> {
public:
    // Bypass digital gain module
    DEF_BIT(0, bypass_digital_gain);
    // Bypass digital gain module
    DEF_BIT(1, bypass_frontend_sensor_offset);
    // Bypass square root function before raw frontend
    DEF_BIT(2, bypass_fe_sqrt);
    // Bypass RAW frontend (green equalization and dynamic defect pixel)
    DEF_BIT(3, bypass_raw_frontend);
    // Bypass static defect pixel
    DEF_BIT(4, bypass_defect_pixel);
};

DEF_NAMESPACE_REG(Top_Bypass1, ping, 0x18eb0)
DEF_NAMESPACE_REG(Top_Bypass1, pong, 0x30e70)

class Top_Bypass2 : public hwreg::RegisterBase<Top_Bypass2, uint32_t> {
public:
    // Bypass sinter
    DEF_BIT(0, bypass_sinter);
    // Bypass temper
    DEF_BIT(1, bypass_temper);
    // Bypass chromatic abberation correction
    DEF_BIT(2, bypass_ca_correction);
};

DEF_NAMESPACE_REG(Top_Bypass2, ping, 0x18eb8)
DEF_NAMESPACE_REG(Top_Bypass2, pong, 0x30e78)

class Top_Bypass3 : public hwreg::RegisterBase<Top_Bypass3, uint32_t> {
public:
    // Bypass backend square
    DEF_BIT(0, bypass_square_be);
    // Bypass sensor offset pre shading
    DEF_BIT(1, bypass_sensor_offset_pre_shading);
    // Bypass radial shading
    DEF_BIT(2, bypass_radial_shading);
    // Bypass mesh ashading
    DEF_BIT(3, bypass_mesh_shading);
    // Bypass white balance
    DEF_BIT(4, bypass_white_balance);
    // Bypass
    DEF_BIT(5, bypass_iridix_gain);
    // Bypass
    DEF_BIT(6, bypass_iridix);
};

DEF_NAMESPACE_REG(Top_Bypass3, ping, 0x18ebc)
DEF_NAMESPACE_REG(Top_Bypass3, pong, 0x30e7c)

class Top_Bypass4 : public hwreg::RegisterBase<Top_Bypass4, uint32_t> {
public:
    // Bypass EW mirror
    DEF_BIT(0, bypass_mirror);
    // Bypass demosaic rgb
    DEF_BIT(1, bypass_demosaic_rgb);
    // Bypass demosaic rgbir
    DEF_BIT(2, bypass_demosaic_rgbir);
    // Bypass pf correction
    DEF_BIT(3, bypass_pf_correction);
    // Bypass CCM
    DEF_BIT(4, bypass_ccm);
    // Bypass CNR
    DEF_BIT(5, bypass_cnr);
    // Bypass 3d lut
    DEF_BIT(6, bypass_3d_lut);
    // Bypass nonequ gamma
    DEF_BIT(7, bypass_nonequ_gamma);
};

DEF_NAMESPACE_REG(Top_Bypass4, ping, 0x18ec0)
DEF_NAMESPACE_REG(Top_Bypass4, pong, 0x30e80)

class Top_BypassFr : public hwreg::RegisterBase<Top_BypassFr, uint32_t> {
public:
    // Bypass fr crop
    DEF_BIT(0, bypass_fr_crop);
    // Bypass fr gamma rgb
    DEF_BIT(1, bypass_fr_gamma_rgb);
    // Bypass fr sharpen
    DEF_BIT(2, bypass_fr_sharpen);
    // Bypass fr cs conv
    DEF_BIT(3, bypass_fr_cs_conv);
};

DEF_NAMESPACE_REG(Top_BypassFr, ping, 0x18ec4)
DEF_NAMESPACE_REG(Top_BypassFr, pong, 0x30e84)

class Top_BypassDs : public hwreg::RegisterBase<Top_BypassDs, uint32_t> {
public:
    // Bypass ds crop
    DEF_BIT(0, bypass_ds_crop);
    // Bypass ds scaler
    DEF_BIT(1, bypass_ds_scaler);
    // Bypass ds gamma rgb
    DEF_BIT(2, bypass_ds_gamma_rgb);
    // Bypass ds sharpen
    DEF_BIT(3, bypass_ds_sharpen);
    // Bypass ds cs conv
    DEF_BIT(4, bypass_ds_cs_conv);
};

DEF_NAMESPACE_REG(Top_BypassDs, ping, 0x18ec8)
DEF_NAMESPACE_REG(Top_BypassDs, pong, 0x30e88)

class Top_Isp : public hwreg::RegisterBase<Top_Isp, uint32_t> {
public:
    //  ISP FR bypass modes.  For debug purposes only. Should be set to 0
    //   during normal operation.
    //         Used to bypass entire ISP after input port or to pass the
    //          stitched image directly to the output. (0=Full
    //          processing, 1=Bypass entire ISP processing and output
    //          [19:4] of raw sensor data After video test gen, 2=Bypass
    //          entire ISP processing and output LSB 10-bits bits of raw
    //          sensor data After video test gen. Data must be MSB
    //          aligned, 3=Reserved 3)
    DEF_FIELD(9, 8, isp_processing_fr_bypass_mode);
    //  Used to select between normal ISP processing with image sensor
    //   data and up to 12 bit RGB input.
    //          In the latler case data is reinserted into pipeline after
    //           purple fringing correction block. (0=Select processed.,
    //           1=Bypass ISP RAW processing.)
    DEF_BIT(0, isp_raw_bypass);
    // 0: Downscale pipeline is enabled
    //      1: Downscale pipeline is disabled. No data is sent out in DMA
    //          and streaming channel (0=Select processed., 1=Bypass ISP
    //          RAW processing.)
    DEF_BIT(1, isp_downscale_pipe_disable);
};

DEF_NAMESPACE_REG(Top_Isp, ping, 0x18ecc)
DEF_NAMESPACE_REG(Top_Isp, pong, 0x30e8c)

class Top_Disable : public hwreg::RegisterBase<Top_Disable, uint32_t> {
public:
    //  AE 5bin histogram tap in the pipeline.  Location of AE statistic
    //   collection. (0=After static white balance whose position is
    //   selected by aexp_src signal, 1=After WDR Frame Stitch. if its
    //   sensor companded data, then use decompanded output. If its
    //   sensor linearised data, then use it directly, 2=After VTPG,
    //   3=reserved)
    DEF_FIELD(2, 1, ae_switch);
    //  AE global histogram tap in the pipeline.  Location of statistics
    //   gathering for 1024 bin global histogram (0=After static white
    //   balance whose position is selected by aexp_src signal, 1=After
    //   WDR Frame Stitch. if its sensor companded data, then use
    //   decompanded output. If its sensor linearised data, then use it
    //   directly, 2=After VTPG, 3=reserved)
    DEF_FIELD(14, 13, aexp_histogram_switch);
    // 0: AEXP 5-bin histogram enabled
    // 1: AEXP 5-bin histogram disabled
    DEF_BIT(0, ae_5bin_hist_disable);
    // 0: AF enabled
    // 1: AF disabled
    DEF_BIT(4, af_disable);
    // AF tap in the pipeline.  . (0=After Sinter, 1=before Sinter)
    DEF_BIT(5, af_switch);
    // 0: AWB enabled
    // 1: AWB disabled
    DEF_BIT(8, awb_disable);
    //  AWB tap in the pipeline.  Location of AWB statistics collection.
    //   (0=Immediately after demosaic, 1=Immediately after CNR)
    DEF_BIT(9, awb_switch);
    // 0: AEXP 1024-bin histogram enabled
    // 1: AEXP 1024-bin histogram disabled
    DEF_BIT(12, aexp_hist_disable);
    //  Post iridix histogram enable.  Enables statistics gathering for
    //   global histogram (0=Enabled, 1=Disabled)
    DEF_BIT(16, ihist_disable);
    // 0=Enabled, 1=Disabled
    DEF_BIT(18, lumavar_disable);
    //  Luma variance tap in the pipeline. (0=Full resolution pipeline,
    //   1=Downscaled pipeline)
    DEF_BIT(19, lumavar_switch);
    //  0=After static white balance when applied before shading, 1=After
    //     static white balance when applied after shading
    DEF_BIT(24, aexp_src);
};

DEF_NAMESPACE_REG(Top_Disable, ping, 0x18ed0)
DEF_NAMESPACE_REG(Top_Disable, pong, 0x30e90)

class Crossbar_Channel : public hwreg::RegisterBase<Crossbar_Channel, uint32_t> {
public:
    // channel0 selection from the input 4 channels
    DEF_FIELD(1, 0, channel1_select);
    // channel1 selection from the input 4 channels
    DEF_FIELD(9, 8, channel2_select);
    // channel2 selection from the input 4 channels
    DEF_FIELD(17, 16, channel3_select);
    // channel4 selection from the input 4 channels
    DEF_FIELD(25, 24, channel4_select);
};

DEF_NAMESPACE_REG(Crossbar_Channel, ping, 0x18ed4)
DEF_NAMESPACE_REG(Crossbar_Channel, pong, 0x30e94)

class VideoTestGenCh0_Select :
      public hwreg::RegisterBase<VideoTestGenCh0_Select, uint32_t> {
public:
    // Test pattern off-on: 0=off, 1=on
    DEF_BIT(0, test_pattern_off_on);
    // Bayer or rgb select for input video: 0=bayer, 1=rgb
    DEF_BIT(1, bayer_rgb_i_sel);
    // Bayer or rgb select for output video: 0=bayer, 1=rgb
    DEF_BIT(2, bayer_rgb_o_sel);
    //  0 = One Shot (on request) generation. 1 = free run (continuous)
    //       generation
    DEF_BIT(3, generate_mode);
    // 0 = Video in interface 1 = Internal Video generation
    DEF_BIT(4, video_source);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_Select, ping, 0x18ed8)
DEF_NAMESPACE_REG(VideoTestGenCh0_Select, pong, 0x30e98)

class VideoTestGenCh0_PatternType :
      public hwreg::RegisterBase<VideoTestGenCh0_PatternType, uint32_t> {
public:
    //  Pattern type select: 0=Flat field,1=Horizontal
    //   gradient,2=Vertical Gradient,3=Vertical
    //   Bars,4=Rectangle,5-255=Default white frame on black
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_PatternType, ping, 0x18edc)
DEF_NAMESPACE_REG(VideoTestGenCh0_PatternType, pong, 0x30e9c)

class VideoTestGenCh0_RBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh0_RBackgnd, uint32_t> {
public:
    // Red background  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RBackgnd, ping, 0x18ee0)
DEF_NAMESPACE_REG(VideoTestGenCh0_RBackgnd, pong, 0x30ea0)

class VideoTestGenCh0_GBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh0_GBackgnd, uint32_t> {
public:
    // Green background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_GBackgnd, ping, 0x18ee4)
DEF_NAMESPACE_REG(VideoTestGenCh0_GBackgnd, pong, 0x30ea4)

class VideoTestGenCh0_BBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh0_BBackgnd, uint32_t> {
public:
    // Blue background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_BBackgnd, ping, 0x18ee8)
DEF_NAMESPACE_REG(VideoTestGenCh0_BBackgnd, pong, 0x30ea8)

class VideoTestGenCh0_RForegnd :
      public hwreg::RegisterBase<VideoTestGenCh0_RForegnd, uint32_t> {
public:
    // Red foreground  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RForegnd, ping, 0x18eec)
DEF_NAMESPACE_REG(VideoTestGenCh0_RForegnd, pong, 0x30eac)

class VideoTestGenCh0_GForegnd :
      public hwreg::RegisterBase<VideoTestGenCh0_GForegnd, uint32_t> {
public:
    // Green foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_GForegnd, ping, 0x18ef0)
DEF_NAMESPACE_REG(VideoTestGenCh0_GForegnd, pong, 0x30eb0)

class VideoTestGenCh0_BForegnd :
      public hwreg::RegisterBase<VideoTestGenCh0_BForegnd, uint32_t> {
public:
    // Blue foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_BForegnd, ping, 0x18ef4)
DEF_NAMESPACE_REG(VideoTestGenCh0_BForegnd, pong, 0x30eb4)

class VideoTestGenCh0_RgbGradient :
      public hwreg::RegisterBase<VideoTestGenCh0_RgbGradient, uint32_t> {
public:
    // RGB gradient increment per pixel (0-15) for first channel
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RgbGradient, ping, 0x18ef8)
DEF_NAMESPACE_REG(VideoTestGenCh0_RgbGradient, pong, 0x30eb8)

class VideoTestGenCh0_RgbGradientStart :
      public hwreg::RegisterBase<VideoTestGenCh0_RgbGradientStart, uint32_t> {
public:
    //  RGB gradient start value for first channel 16bit, MSB aligned to
    //   used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RgbGradientStart, ping, 0x18efc)
DEF_NAMESPACE_REG(VideoTestGenCh0_RgbGradientStart, pong, 0x30ebc)

class VideoTestGenCh0_RectTB :
      public hwreg::RegisterBase<VideoTestGenCh0_RectTB, uint32_t> {
public:
    // Rectangle top line number 1-n
    DEF_FIELD(13, 0, rect_top);
    // Rectangle bottom line number 1-n
    DEF_FIELD(29, 16, rect_bot);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RectTB, ping, 0x18f00)
DEF_NAMESPACE_REG(VideoTestGenCh0_RectTB, pong, 0x30ec0)

class VideoTestGenCh0_RectRL :
      public hwreg::RegisterBase<VideoTestGenCh0_RectRL, uint32_t> {
public:
    // Rectangle left pixel number 1-n
    DEF_FIELD(13, 0, rect_left);
    // Rectangle right pixel number 1-n
    DEF_FIELD(29, 16, rect_right);
};

DEF_NAMESPACE_REG(VideoTestGenCh0_RectRL, ping, 0x18f04)
DEF_NAMESPACE_REG(VideoTestGenCh0_RectRL, pong, 0x30ec4)

class VideoTestGenCh1_Select :
      public hwreg::RegisterBase<VideoTestGenCh1_Select, uint32_t> {
public:
    // Test pattern off-on: 0=off, 1=on
    DEF_BIT(0, test_pattern_off_on);
    // Bayer or rgb select for input video: 0=bayer, 1=rgb
    DEF_BIT(1, bayer_rgb_i_sel);
    // Bayer or rgb select for output video: 0=bayer, 1=rgb
    DEF_BIT(2, bayer_rgb_o_sel);
    //  0 = One Shot (on request) generation. 1 = free run (continuous)
    //       generation
    DEF_BIT(3, generate_mode);
    // 0 = Video in interface 1 = Internal Video generation
    DEF_BIT(4, video_source);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_Select, ping, 0x18f08)
DEF_NAMESPACE_REG(VideoTestGenCh1_Select, pong, 0x30ec8)

class VideoTestGenCh1_PatternType :
      public hwreg::RegisterBase<VideoTestGenCh1_PatternType, uint32_t> {
public:
    //  Pattern type select: 0=Flat field,1=Horizontal
    //   gradient,2=Vertical Gradient,3=Vertical
    //   Bars,4=Rectangle,5-255=Default white frame on black
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_PatternType, ping, 0x18f0c)
DEF_NAMESPACE_REG(VideoTestGenCh1_PatternType, pong, 0x30ecc)

class VideoTestGenCh1_RBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh1_RBackgnd, uint32_t> {
public:
    // Red background  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RBackgnd, ping, 0x18f10)
DEF_NAMESPACE_REG(VideoTestGenCh1_RBackgnd, pong, 0x30ed0)

class VideoTestGenCh1_GBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh1_GBackgnd, uint32_t> {
public:
    // Green background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_GBackgnd, ping, 0x18f14)
DEF_NAMESPACE_REG(VideoTestGenCh1_GBackgnd, pong, 0x30ed4)

class VideoTestGenCh1_BBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh1_BBackgnd, uint32_t> {
public:
    // Blue background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_BBackgnd, ping, 0x18f18)
DEF_NAMESPACE_REG(VideoTestGenCh1_BBackgnd, pong, 0x30ed8)

class VideoTestGenCh1_RForegnd :
      public hwreg::RegisterBase<VideoTestGenCh1_RForegnd, uint32_t> {
public:
    // Red foreground  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RForegnd, ping, 0x18f1c)
DEF_NAMESPACE_REG(VideoTestGenCh1_RForegnd, pong, 0x30edc)

class VideoTestGenCh1_GForegnd :
      public hwreg::RegisterBase<VideoTestGenCh1_GForegnd, uint32_t> {
public:
    // Green foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_GForegnd, ping, 0x18f20)
DEF_NAMESPACE_REG(VideoTestGenCh1_GForegnd, pong, 0x30ee0)

class VideoTestGenCh1_BForegnd :
      public hwreg::RegisterBase<VideoTestGenCh1_BForegnd, uint32_t> {
public:
    // Blue foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_BForegnd, ping, 0x18f24)
DEF_NAMESPACE_REG(VideoTestGenCh1_BForegnd, pong, 0x30ee4)

class VideoTestGenCh1_RgbGradient :
      public hwreg::RegisterBase<VideoTestGenCh1_RgbGradient, uint32_t> {
public:
    // RGB gradient increment per pixel (0-15) for first channel
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RgbGradient, ping, 0x18f28)
DEF_NAMESPACE_REG(VideoTestGenCh1_RgbGradient, pong, 0x30ee8)

class VideoTestGenCh1_RgbGradientStart :
      public hwreg::RegisterBase<VideoTestGenCh1_RgbGradientStart, uint32_t> {
public:
    //  RGB gradient start value for first channel 16bit, MSB aligned to
    //   used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RgbGradientStart, ping, 0x18f2c)
DEF_NAMESPACE_REG(VideoTestGenCh1_RgbGradientStart, pong, 0x30eec)

class VideoTestGenCh1_RectTB :
      public hwreg::RegisterBase<VideoTestGenCh1_RectTB, uint32_t> {
public:
    // Rectangle top line number 1-n
    DEF_FIELD(13, 0, rect_top);
    // Rectangle bottom line number 1-n
    DEF_FIELD(29, 16, rect_bot);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RectTB, ping, 0x18f30)
DEF_NAMESPACE_REG(VideoTestGenCh1_RectTB, pong, 0x30ef0)

class VideoTestGenCh1_RectRL :
      public hwreg::RegisterBase<VideoTestGenCh1_RectRL, uint32_t> {
public:
    // Rectangle left pixel number 1-n
    DEF_FIELD(13, 0, rect_left);
    // Rectangle right pixel number 1-n
    DEF_FIELD(29, 16, rect_right);
};

DEF_NAMESPACE_REG(VideoTestGenCh1_RectRL, ping, 0x18f34)
DEF_NAMESPACE_REG(VideoTestGenCh1_RectRL, pong, 0x30ef4)

class VideoTestGenCh2_Select :
      public hwreg::RegisterBase<VideoTestGenCh2_Select, uint32_t> {
public:
    // Test pattern off-on: 0=off, 1=on
    DEF_BIT(0, test_pattern_off_on);
    // Bayer or rgb select for input video: 0=bayer, 1=rgb
    DEF_BIT(1, bayer_rgb_i_sel);
    // Bayer or rgb select for output video: 0=bayer, 1=rgb
    DEF_BIT(2, bayer_rgb_o_sel);
    //  0 = One Shot (on request) generation. 1 = free run (continuous)
    //       generation
    DEF_BIT(3, generate_mode);
    // 0 = Video in interface 1 = Internal Video generation
    DEF_BIT(4, video_source);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_Select, ping, 0x18f38)
DEF_NAMESPACE_REG(VideoTestGenCh2_Select, pong, 0x30ef8)

class VideoTestGenCh2_PatternType :
      public hwreg::RegisterBase<VideoTestGenCh2_PatternType, uint32_t> {
public:
    //  Pattern type select: 0=Flat field,1=Horizontal
    //   gradient,2=Vertical Gradient,3=Vertical
    //   Bars,4=Rectangle,5-255=Default white frame on black
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_PatternType, ping, 0x18f3c)
DEF_NAMESPACE_REG(VideoTestGenCh2_PatternType, pong, 0x30efc)

class VideoTestGenCh2_RBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh2_RBackgnd, uint32_t> {
public:
    // Red background  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RBackgnd, ping, 0x18f40)
DEF_NAMESPACE_REG(VideoTestGenCh2_RBackgnd, pong, 0x30f00)

class VideoTestGenCh2_GBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh2_GBackgnd, uint32_t> {
public:
    // Green background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_GBackgnd, ping, 0x18f44)
DEF_NAMESPACE_REG(VideoTestGenCh2_GBackgnd, pong, 0x30f04)

class VideoTestGenCh2_BBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh2_BBackgnd, uint32_t> {
public:
    // Blue background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_BBackgnd, ping, 0x18f48)
DEF_NAMESPACE_REG(VideoTestGenCh2_BBackgnd, pong, 0x30f08)

class VideoTestGenCh2_RForegnd :
      public hwreg::RegisterBase<VideoTestGenCh2_RForegnd, uint32_t> {
public:
    // Red foreground  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RForegnd, ping, 0x18f4c)
DEF_NAMESPACE_REG(VideoTestGenCh2_RForegnd, pong, 0x30f0c)

class VideoTestGenCh2_GForegnd :
      public hwreg::RegisterBase<VideoTestGenCh2_GForegnd, uint32_t> {
public:
    // Green foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_GForegnd, ping, 0x18f50)
DEF_NAMESPACE_REG(VideoTestGenCh2_GForegnd, pong, 0x30f10)

class VideoTestGenCh2_BForegnd :
      public hwreg::RegisterBase<VideoTestGenCh2_BForegnd, uint32_t> {
public:
    // Blue foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_BForegnd, ping, 0x18f54)
DEF_NAMESPACE_REG(VideoTestGenCh2_BForegnd, pong, 0x30f14)

class VideoTestGenCh2_RgbGradient :
      public hwreg::RegisterBase<VideoTestGenCh2_RgbGradient, uint32_t> {
public:
    // RGB gradient increment per pixel (0-15) for first channel
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RgbGradient, ping, 0x18f58)
DEF_NAMESPACE_REG(VideoTestGenCh2_RgbGradient, pong, 0x30f18)

class VideoTestGenCh2_RgbGradientStart :
      public hwreg::RegisterBase<VideoTestGenCh2_RgbGradientStart, uint32_t> {
public:
    //  RGB gradient start value for first channel 16bit, MSB aligned to
    //   used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RgbGradientStart, ping, 0x18f5c)
DEF_NAMESPACE_REG(VideoTestGenCh2_RgbGradientStart, pong, 0x30f1c)

class VideoTestGenCh2_RectTB :
      public hwreg::RegisterBase<VideoTestGenCh2_RectTB, uint32_t> {
public:
    // Rectangle top line number 1-n
    DEF_FIELD(13, 0, rect_top);
    // Rectangle bottom line number 1-n
    DEF_FIELD(29, 16, rect_bot);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RectTB, ping, 0x18f60)
DEF_NAMESPACE_REG(VideoTestGenCh2_RectTB, pong, 0x30f20)

class VideoTestGenCh2_RectRL :
      public hwreg::RegisterBase<VideoTestGenCh2_RectRL, uint32_t> {
public:
    // Rectangle left pixel number 1-n
    DEF_FIELD(13, 0, rect_left);
    // Rectangle right pixel number 1-n
    DEF_FIELD(29, 16, rect_right);
};

DEF_NAMESPACE_REG(VideoTestGenCh2_RectRL, ping, 0x18f64)
DEF_NAMESPACE_REG(VideoTestGenCh2_RectRL, pong, 0x30f24)

class VideoTestGenCh3_Select :
      public hwreg::RegisterBase<VideoTestGenCh3_Select, uint32_t> {
public:
    // Test pattern off-on: 0=off, 1=on
    DEF_BIT(0, test_pattern_off_on);
    // Bayer or rgb select for input video: 0=bayer, 1=rgb
    DEF_BIT(1, bayer_rgb_i_sel);
    // Bayer or rgb select for output video: 0=bayer, 1=rgb
    DEF_BIT(2, bayer_rgb_o_sel);
    //  0 = One Shot (on request) generation. 1 = free run (continuous)
    //       generation
    DEF_BIT(3, generate_mode);
    // 0 = Video in interface 1 = Internal Video generation
    DEF_BIT(4, video_source);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_Select, ping, 0x18f68)
DEF_NAMESPACE_REG(VideoTestGenCh3_Select, pong, 0x30f28)

class VideoTestGenCh3_PatternType :
      public hwreg::RegisterBase<VideoTestGenCh3_PatternType, uint32_t> {
public:
    //  Pattern type select: 0=Flat field,1=Horizontal
    //   gradient,2=Vertical Gradient,3=Vertical
    //   Bars,4=Rectangle,5-255=Default white frame on black
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_PatternType, ping, 0x18f6c)
DEF_NAMESPACE_REG(VideoTestGenCh3_PatternType, pong, 0x30f2c)

class VideoTestGenCh3_RBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh3_RBackgnd, uint32_t> {
public:
    // Red background  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RBackgnd, ping, 0x18f70)
DEF_NAMESPACE_REG(VideoTestGenCh3_RBackgnd, pong, 0x30f30)

class VideoTestGenCh3_GBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh3_GBackgnd, uint32_t> {
public:
    // Green background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_GBackgnd, ping, 0x18f74)
DEF_NAMESPACE_REG(VideoTestGenCh3_GBackgnd, pong, 0x30f34)

class VideoTestGenCh3_BBackgnd :
      public hwreg::RegisterBase<VideoTestGenCh3_BBackgnd, uint32_t> {
public:
    // Blue background value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_BBackgnd, ping, 0x18f78)
DEF_NAMESPACE_REG(VideoTestGenCh3_BBackgnd, pong, 0x30f38)

class VideoTestGenCh3_RForegnd :
      public hwreg::RegisterBase<VideoTestGenCh3_RForegnd, uint32_t> {
public:
    // Red foreground  value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RForegnd, ping, 0x18f7c)
DEF_NAMESPACE_REG(VideoTestGenCh3_RForegnd, pong, 0x30f3c)

class VideoTestGenCh3_GForegnd :
      public hwreg::RegisterBase<VideoTestGenCh3_GForegnd, uint32_t> {
public:
    // Green foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_GForegnd, ping, 0x18f80)
DEF_NAMESPACE_REG(VideoTestGenCh3_GForegnd, pong, 0x30f40)

class VideoTestGenCh3_BForegnd :
      public hwreg::RegisterBase<VideoTestGenCh3_BForegnd, uint32_t> {
public:
    // Blue foreground value 16bit, MSB aligned to used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_BForegnd, ping, 0x18f84)
DEF_NAMESPACE_REG(VideoTestGenCh3_BForegnd, pong, 0x30f44)

class VideoTestGenCh3_RgbGradient :
      public hwreg::RegisterBase<VideoTestGenCh3_RgbGradient, uint32_t> {
public:
    // RGB gradient increment per pixel (0-15) for first channel
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RgbGradient, ping, 0x18f88)
DEF_NAMESPACE_REG(VideoTestGenCh3_RgbGradient, pong, 0x30f48)

class VideoTestGenCh3_RgbGradientStart :
      public hwreg::RegisterBase<VideoTestGenCh3_RgbGradientStart, uint32_t> {
public:
    //  RGB gradient start value for first channel 16bit, MSB aligned to
    //   used width
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RgbGradientStart, ping, 0x18f8c)
DEF_NAMESPACE_REG(VideoTestGenCh3_RgbGradientStart, pong, 0x30f4c)

class VideoTestGenCh3_RectTB :
      public hwreg::RegisterBase<VideoTestGenCh3_RectTB, uint32_t> {
public:
    // Rectangle top line number 1-n
    DEF_FIELD(13, 0, rect_top);
    // Rectangle bottom line number 1-n
    DEF_FIELD(29, 16, rect_bot);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RectTB, ping, 0x18f90)
DEF_NAMESPACE_REG(VideoTestGenCh3_RectTB, pong, 0x30f50)

class VideoTestGenCh3_RectRL :
      public hwreg::RegisterBase<VideoTestGenCh3_RectRL, uint32_t> {
public:
    // Rectangle left pixel number 1-n
    DEF_FIELD(13, 0, rect_left);
    // Rectangle right pixel number 1-n
    DEF_FIELD(29, 16, rect_right);
};

DEF_NAMESPACE_REG(VideoTestGenCh3_RectRL, ping, 0x18f94)
DEF_NAMESPACE_REG(VideoTestGenCh3_RectRL, pong, 0x30f54)

class InputFormatter_Mode : public hwreg::RegisterBase<InputFormatter_Mode, uint32_t> {
public:
    //  Input mode (0=Linear data, 1=2:3 multiple exposure multiplexing,
    //   2=Logarithmic encoding, 3=Companding curve with knee points,
    //   4=16bit linear+ 12bit VS, 5=12bit companded + 12bit VS,
    //   6=Reserved, 7=pass through mode)
    DEF_FIELD(2, 0, mode_in);
    //  Input bitwidth select (0=8 bits, 1=10 bits, 2=12 bits, 3=14 bits,
    //   4=16 bits, 5=20 bits (no 18 bits), 6=Reserved 6, 7=Reserved 7)
    DEF_FIELD(18, 16, input_bitwidth_select);
};

DEF_NAMESPACE_REG(InputFormatter_Mode, ping, 0x18f98)
DEF_NAMESPACE_REG(InputFormatter_Mode, pong, 0x30f58)

class InputFormatter_FactorMl :
      public hwreg::RegisterBase<InputFormatter_FactorMl, uint32_t> {
public:
    //  18 bit, 6.12 fix point - ratio between long and medium exposure
    //      for 2:3 multiplexed mode
    DEF_FIELD(17, 0, value);
};

DEF_NAMESPACE_REG(InputFormatter_FactorMl, ping, 0x18f9c)
DEF_NAMESPACE_REG(InputFormatter_FactorMl, pong, 0x30f5c)

class InputFormatter_FactorMs :
      public hwreg::RegisterBase<InputFormatter_FactorMs, uint32_t> {
public:
    //  13 bit, 1.12 fix point - ratio between short and medium exposure
    //      for 2:3 multiplexed mode
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(InputFormatter_FactorMs, ping, 0x18fa0)
DEF_NAMESPACE_REG(InputFormatter_FactorMs, pong, 0x30f60)

class InputFormatter_BlackLevel :
      public hwreg::RegisterBase<InputFormatter_BlackLevel, uint32_t> {
public:
    // Black level of sensor data for 2:3 multiplexed mode
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(InputFormatter_BlackLevel, ping, 0x18fa4)
DEF_NAMESPACE_REG(InputFormatter_BlackLevel, pong, 0x30f64)

class InputFormatter_KneePoint :
      public hwreg::RegisterBase<InputFormatter_KneePoint, uint32_t> {
public:
    // First knee point
    DEF_FIELD(15, 0, knee_point0);
    // Second knee point
    DEF_FIELD(31, 16, knee_point1);
};

DEF_NAMESPACE_REG(InputFormatter_KneePoint, ping, 0x18fa8)
DEF_NAMESPACE_REG(InputFormatter_KneePoint, pong, 0x30f68)

class InputFormatter_KneePoint2 :
      public hwreg::RegisterBase<InputFormatter_KneePoint2, uint32_t> {
public:
    // Third knee point
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(InputFormatter_KneePoint2, ping, 0x18fac)
DEF_NAMESPACE_REG(InputFormatter_KneePoint2, pong, 0x30f6c)

class InputFormatter_Slope : public hwreg::RegisterBase<InputFormatter_Slope, uint32_t> {
public:
    //  First slope for companding table segments (0=1x, 1=2x, 2=4x,
    //   3=8x, 4=16x, 5=32x, 6=64x, 7=128x, 8=256x, 9=512x, 10=1024x,
    //   11=2048x, 12=4096x, 13=8192x, 14=16384x, 15=32768x)
    DEF_FIELD(3, 0, slope0_select);
    //  Second slope for companding table segments (encoding is the same
    //   as slope0 select)
    DEF_FIELD(11, 8, slope1_select);
    //  Third slope for companding table segments (encoding is the same
    //   as slope0 select)
    DEF_FIELD(19, 16, slope2_select);
    //  Last slope for companding table segments (encoding is the same as
    //   slope0 select)
    DEF_FIELD(27, 24, slope3_select);
};

DEF_NAMESPACE_REG(InputFormatter_Slope, ping, 0x18fb0)
DEF_NAMESPACE_REG(InputFormatter_Slope, pong, 0x30f70)

class SensorOffsetWdrL_Offset0 :
      public hwreg::RegisterBase<SensorOffsetWdrL_Offset0, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(11, 0, offset_00);
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(27, 16, offset_01);
};

DEF_NAMESPACE_REG(SensorOffsetWdrL_Offset0, ping, 0x18fb4)
DEF_NAMESPACE_REG(SensorOffsetWdrL_Offset0, pong, 0x30f74)

class SensorOffsetWdrL_Offset1 :
      public hwreg::RegisterBase<SensorOffsetWdrL_Offset1, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(11, 0, offset_10);
    // offset offset for color channel 11 (B)
    DEF_FIELD(27, 16, offset_11);
};

DEF_NAMESPACE_REG(SensorOffsetWdrL_Offset1, ping, 0x18fb8)
DEF_NAMESPACE_REG(SensorOffsetWdrL_Offset1, pong, 0x30f78)

class SensorOffsetWdrM_Offset0 :
      public hwreg::RegisterBase<SensorOffsetWdrM_Offset0, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(11, 0, offset_00);
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(27, 16, offset_01);
};

DEF_NAMESPACE_REG(SensorOffsetWdrM_Offset0, ping, 0x18fbc)
DEF_NAMESPACE_REG(SensorOffsetWdrM_Offset0, pong, 0x30f7c)

class SensorOffsetWdrM_Offset1 :
      public hwreg::RegisterBase<SensorOffsetWdrM_Offset1, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(11, 0, offset_10);
    // offset offset for color channel 11 (B)
    DEF_FIELD(27, 16, offset_11);
};

DEF_NAMESPACE_REG(SensorOffsetWdrM_Offset1, ping, 0x18fc0)
DEF_NAMESPACE_REG(SensorOffsetWdrM_Offset1, pong, 0x30f80)

class SensorOffsetWdrS_Offset0 :
      public hwreg::RegisterBase<SensorOffsetWdrS_Offset0, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(11, 0, offset_00);
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(27, 16, offset_01);
};

DEF_NAMESPACE_REG(SensorOffsetWdrS_Offset0, ping, 0x18fc4)
DEF_NAMESPACE_REG(SensorOffsetWdrS_Offset0, pong, 0x30f84)

class SensorOffsetWdrS_Offset1 :
      public hwreg::RegisterBase<SensorOffsetWdrS_Offset1, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(11, 0, offset_10);
    // offset offset for color channel 11 (B)
    DEF_FIELD(27, 16, offset_11);
};

DEF_NAMESPACE_REG(SensorOffsetWdrS_Offset1, ping, 0x18fc8)
DEF_NAMESPACE_REG(SensorOffsetWdrS_Offset1, pong, 0x30f88)

class SensorOffsetWdrVs_Offset0 :
      public hwreg::RegisterBase<SensorOffsetWdrVs_Offset0, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(11, 0, offset_00);
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(27, 16, offset_01);
};

DEF_NAMESPACE_REG(SensorOffsetWdrVs_Offset0, ping, 0x18fcc)
DEF_NAMESPACE_REG(SensorOffsetWdrVs_Offset0, pong, 0x30f8c)

class SensorOffsetWdrVs_Offset1 :
      public hwreg::RegisterBase<SensorOffsetWdrVs_Offset1, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(11, 0, offset_10);
    // offset offset for color channel 11 (B)
    DEF_FIELD(27, 16, offset_11);
};

DEF_NAMESPACE_REG(SensorOffsetWdrVs_Offset1, ping, 0x18fd0)
DEF_NAMESPACE_REG(SensorOffsetWdrVs_Offset1, pong, 0x30f90)

class WideDynamicRangeGain_Gain0 :
      public hwreg::RegisterBase<WideDynamicRangeGain_Gain0, uint32_t> {
public:
    // Gain applied to ch-long data in 5.8 format
    DEF_FIELD(12, 0, gain_l);
    // Gain applied to ch-medium data in 5.8 format
    DEF_FIELD(28, 16, gain_m);
};

DEF_NAMESPACE_REG(WideDynamicRangeGain_Gain0, ping, 0x18fd4)
DEF_NAMESPACE_REG(WideDynamicRangeGain_Gain0, pong, 0x30f94)

class WideDynamicRangeGain_Gain1 :
      public hwreg::RegisterBase<WideDynamicRangeGain_Gain1, uint32_t> {
public:
    // Gain applied to ch-short data in 5.8 format
    DEF_FIELD(12, 0, gain_s);
    // Gain applied to ch-vs data in 5.8 format
    DEF_FIELD(28, 16, gain_vs);
};

DEF_NAMESPACE_REG(WideDynamicRangeGain_Gain1, ping, 0x18fd8)
DEF_NAMESPACE_REG(WideDynamicRangeGain_Gain1, pong, 0x30f98)

class WideDynamicRangeGain_BlackLevel0 :
      public hwreg::RegisterBase<WideDynamicRangeGain_BlackLevel0, uint32_t> {
public:
    // Sensor offset applied to ch-long data
    DEF_FIELD(11, 0, black_level_l);
    // Sensor offset applied to ch-medium data
    DEF_FIELD(27, 16, black_level_m);
};

DEF_NAMESPACE_REG(WideDynamicRangeGain_BlackLevel0, ping, 0x18fdc)
DEF_NAMESPACE_REG(WideDynamicRangeGain_BlackLevel0, pong, 0x30f9c)

class WideDynamicRangeGain_BlackLevel1 :
      public hwreg::RegisterBase<WideDynamicRangeGain_BlackLevel1, uint32_t> {
public:
    // Sensor offset applied to ch-short data
    DEF_FIELD(11, 0, black_level_s);
    // Sensor offset applied to ch-veryshort data
    DEF_FIELD(27, 16, black_level_vs);
};

DEF_NAMESPACE_REG(WideDynamicRangeGain_BlackLevel1, ping, 0x18fe0)
DEF_NAMESPACE_REG(WideDynamicRangeGain_BlackLevel1, pong, 0x30fa0)

class FrameStitch_Mode : public hwreg::RegisterBase<FrameStitch_Mode, uint32_t> {
public:
    // 0 : 4-exposure
    // 1 : 2-exposure
    // 2 : 3-exposure
    // 3 : 4-exposure
    DEF_FIELD(1, 0, mode_in);
    //  This register is only for debug purpose. for normal operation it
    //   must be kept in its default value (0)
    //     0 : normal stitched output
    //     1 : long data routed out
    //     2 : medium data routed out
    //     4 : short data routed out
    //     8 : very short data routed out
    //     16: LM stitched output taken out
    //     32: MS stitched output taken out
    //     64: SVS stitched output taken out
    //     others: reserved
    DEF_FIELD(15, 8, output_select);
};

DEF_NAMESPACE_REG(FrameStitch_Mode, ping, 0x18fe4)
DEF_NAMESPACE_REG(FrameStitch_Mode, pong, 0x30fa4)

class FrameStitch_ExposureRatio :
      public hwreg::RegisterBase<FrameStitch_ExposureRatio, uint32_t> {
public:
    //  Sets ratio between long and medium exposures - this must match
    //   the actual exposure ratio on the sensor
    DEF_FIELD(11, 0, lm_exposure_ratio);
    //  Sets ratio between medium and short exposures - this must match
    //   the actual exposure ratio on the sensor
    DEF_FIELD(27, 16, ms_exposure_ratio);
};

DEF_NAMESPACE_REG(FrameStitch_ExposureRatio, ping, 0x18fe8)
DEF_NAMESPACE_REG(FrameStitch_ExposureRatio, pong, 0x30fa8)

class FrameStitch_SvsExposureRatio :
      public hwreg::RegisterBase<FrameStitch_SvsExposureRatio, uint32_t> {
public:
    //  Sets ratio between short and very short exposures - this must
    //   match the actual exposure ratio on the sensor
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_SvsExposureRatio, ping, 0x18fec)
DEF_NAMESPACE_REG(FrameStitch_SvsExposureRatio, pong, 0x30fac)

class FrameStitch_LongMediumThresh :
      public hwreg::RegisterBase<FrameStitch_LongMediumThresh, uint32_t> {
public:
    //  These two thresholds are for LM pairs. Both are with respect to
    //   the longer stitches.
    //     Data above this threshold will be taken from short exposure only
    DEF_FIELD(11, 0, lm_thresh_high);
    // Data below this threshold will be taken from long exposure only
    DEF_FIELD(27, 16, lm_thresh_low);
};

DEF_NAMESPACE_REG(FrameStitch_LongMediumThresh, ping, 0x18ff0)
DEF_NAMESPACE_REG(FrameStitch_LongMediumThresh, pong, 0x30fb0)

class FrameStitch_MediumShortThresh :
      public hwreg::RegisterBase<FrameStitch_MediumShortThresh, uint32_t> {
public:
    //  These two thresholds are for MS pairs. Both are with respect to
    //   the longer stitches.
    //     Data above this threshold will be taken from short exposure only
    DEF_FIELD(11, 0, ms_thresh_high);
    // Data below this threshold will be taken from long exposure only
    DEF_FIELD(27, 16, ms_thresh_low);
};

DEF_NAMESPACE_REG(FrameStitch_MediumShortThresh, ping, 0x18ff4)
DEF_NAMESPACE_REG(FrameStitch_MediumShortThresh, pong, 0x30fb4)

class FrameStitch_ShortVeryShortThresh :
      public hwreg::RegisterBase<FrameStitch_ShortVeryShortThresh, uint32_t> {
public:
    //  These two thresholds are for SVS pairs. Both are with respect to
    //   the longer stitches.
    //     Data above this threshold will be taken from short exposure only
    DEF_FIELD(11, 0, svs_thresh_high);
    // Data below this threshold will be taken from long exposure only
    DEF_FIELD(27, 16, svs_thresh_low);
};

DEF_NAMESPACE_REG(FrameStitch_ShortVeryShortThresh, ping, 0x18ff8)
DEF_NAMESPACE_REG(FrameStitch_ShortVeryShortThresh, pong, 0x30fb8)

class FrameStitch_BlackLevel0 :
      public hwreg::RegisterBase<FrameStitch_BlackLevel0, uint32_t> {
public:
    // Black level for long exposure input
    DEF_FIELD(11, 0, black_level_long);
    // Black level for medium exposure input
    //     *** NOTE ***:
    //      If the wdr unit is configured to use as 2-exposure, THIS
    //       REGISTER POSITION must contain the black level of
    //     short exposure as the LM pair is used for all other configurations
    DEF_FIELD(27, 16, black_level_medium);
};

DEF_NAMESPACE_REG(FrameStitch_BlackLevel0, ping, 0x18ffc)
DEF_NAMESPACE_REG(FrameStitch_BlackLevel0, pong, 0x30fbc)

class FrameStitch_BlackLevel1 :
      public hwreg::RegisterBase<FrameStitch_BlackLevel1, uint32_t> {
public:
    // Black level for short exposure input
    DEF_FIELD(11, 0, black_level_short);
    // Black level for very short exposure input
    DEF_FIELD(27, 16, black_level_very_short);
};

DEF_NAMESPACE_REG(FrameStitch_BlackLevel1, ping, 0x19000)
DEF_NAMESPACE_REG(FrameStitch_BlackLevel1, pong, 0x30fc0)

class FrameStitch_BlackLevelOut :
      public hwreg::RegisterBase<FrameStitch_BlackLevelOut, uint32_t> {
public:
    // Black level for module output
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_BlackLevelOut, ping, 0x19004)
DEF_NAMESPACE_REG(FrameStitch_BlackLevelOut, pong, 0x30fc4)

class FrameStitch_Config0 : public hwreg::RegisterBase<FrameStitch_Config0, uint32_t> {
public:
    //  The noise profile weights are multiplied by this value to give
    //   expected noise amplitude.
    DEF_FIELD(11, 0, lm_np_mult);
    //  The noise profile weights are multiplied by this value to give
    //   expected noise amplitude.
    DEF_FIELD(27, 16, ms_np_mult);
};

DEF_NAMESPACE_REG(FrameStitch_Config0, ping, 0x19008)
DEF_NAMESPACE_REG(FrameStitch_Config0, pong, 0x30fc8)

class FrameStitch_Config1 : public hwreg::RegisterBase<FrameStitch_Config1, uint32_t> {
public:
    //  The noise profile weights are multiplied by this value to give
    //   expected noise amplitude.
    DEF_FIELD(11, 0, svs_np_mult);
    //  This defines the gradient of the motion alpha ramp. Higher values
    //   mean a steeper ramp and so a more rapid transition between
    //      non-motion-corrected and motion-corrected regions.
    DEF_FIELD(27, 16, lm_alpha_mov_slope);
};

DEF_NAMESPACE_REG(FrameStitch_Config1, ping, 0x1900c)
DEF_NAMESPACE_REG(FrameStitch_Config1, pong, 0x30fcc)

class FrameStitch_Config2 : public hwreg::RegisterBase<FrameStitch_Config2, uint32_t> {
public:
    //  his defines the gradient of the motion alpha ramp. Higher values
    //   mean a steeper ramp and so a more rapid transition between
    //      non-motion-corrected and motion-corrected regions.
    DEF_FIELD(11, 0, ms_alpha_mov_slope);
    //  his defines the gradient of the motion alpha ramp. Higher values
    //   mean a steeper ramp and so a more rapid transition between
    //      non-motion-corrected and motion-corrected regions.
    DEF_FIELD(27, 16, svs_alpha_mov_slope);
};

DEF_NAMESPACE_REG(FrameStitch_Config2, ping, 0x19010)
DEF_NAMESPACE_REG(FrameStitch_Config2, pong, 0x30fd0)

class FrameStitch_GainRB : public hwreg::RegisterBase<FrameStitch_GainRB, uint32_t> {
public:
    // Multiplier for color channel R
    DEF_FIELD(11, 0, gain_r);
    // Multiplier for color channel B
    DEF_FIELD(27, 16, gain_b);
};

DEF_NAMESPACE_REG(FrameStitch_GainRB, ping, 0x19014)
DEF_NAMESPACE_REG(FrameStitch_GainRB, pong, 0x30fd4)

class FrameStitch_ConsistencyThreshMov :
      public hwreg::RegisterBase<FrameStitch_ConsistencyThreshMov, uint32_t> {
public:
    // Pixel consistency reporting - motion threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_ConsistencyThreshMov, ping, 0x19018)
DEF_NAMESPACE_REG(FrameStitch_ConsistencyThreshMov, pong, 0x30fd8)

class FrameStitch_ConsistencyThreshLvl :
      public hwreg::RegisterBase<FrameStitch_ConsistencyThreshLvl, uint32_t> {
public:
    // Pixel consistency reporting - flicker threshold
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_ConsistencyThreshLvl, ping, 0x1901c)
DEF_NAMESPACE_REG(FrameStitch_ConsistencyThreshLvl, pong, 0x30fdc)

class FrameStitch_Lm : public hwreg::RegisterBase<FrameStitch_Lm, uint32_t> {
public:
    //  Higher values make it more likely to interpret differences
    //   between the long and medium exposures as noise (and thus do no
    //   motion correction).
    DEF_FIELD(5, 0, lm_noise_thresh);
    //  Lower values make it more likely to interpret  differences
    //   between the long and medium exposures as noise (and thus do no
    //   motion correction).
    DEF_FIELD(13, 8, lm_pos_weight);
    //  Higher values make it more likely to interpret differences
    //   between the long and medium exposures as noise (and thus do no
    //   motion correction).
    DEF_FIELD(21, 16, lm_neg_weight);
};

DEF_NAMESPACE_REG(FrameStitch_Lm, ping, 0x19020)
DEF_NAMESPACE_REG(FrameStitch_Lm, pong, 0x30fe0)

class FrameStitch_LmMedNoise :
      public hwreg::RegisterBase<FrameStitch_LmMedNoise, uint32_t> {
public:
    DEF_FIELD(11, 0, lm_med_noise_alpha_thresh);
    DEF_FIELD(27, 16, lm_med_noise_intensity_thresh);
};

DEF_NAMESPACE_REG(FrameStitch_LmMedNoise, ping, 0x19024)
DEF_NAMESPACE_REG(FrameStitch_LmMedNoise, pong, 0x30fe4)

class FrameStitch_LmMcBlendSlope :
      public hwreg::RegisterBase<FrameStitch_LmMcBlendSlope, uint32_t> {
public:
    DEF_FIELD(21, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcBlendSlope, ping, 0x19028)
DEF_NAMESPACE_REG(FrameStitch_LmMcBlendSlope, pong, 0x30fe8)

class FrameStitch_LmMcBlend :
      public hwreg::RegisterBase<FrameStitch_LmMcBlend, uint32_t> {
public:
    DEF_FIELD(7, 0, lm_mc_blend_thresh);
    DEF_FIELD(27, 16, lm_mc_blend_offset);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcBlend, ping, 0x1902c)
DEF_NAMESPACE_REG(FrameStitch_LmMcBlend, pong, 0x30fec)

class FrameStitch_LmMcThreshSlope :
      public hwreg::RegisterBase<FrameStitch_LmMcThreshSlope, uint32_t> {
public:
    DEF_FIELD(21, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcThreshSlope, ping, 0x19030)
DEF_NAMESPACE_REG(FrameStitch_LmMcThreshSlope, pong, 0x30ff0)

class FrameStitch_LmMcThreshThresh :
      public hwreg::RegisterBase<FrameStitch_LmMcThreshThresh, uint32_t> {
public:
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcThreshThresh, ping, 0x19034)
DEF_NAMESPACE_REG(FrameStitch_LmMcThreshThresh, pong, 0x30ff4)

class FrameStitch_LmMcThreshOffset :
      public hwreg::RegisterBase<FrameStitch_LmMcThreshOffset, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcThreshOffset, ping, 0x19038)
DEF_NAMESPACE_REG(FrameStitch_LmMcThreshOffset, pong, 0x30ff8)

class FrameStitch_LmMcMagThreshSlope :
      public hwreg::RegisterBase<FrameStitch_LmMcMagThreshSlope, uint32_t> {
public:
    DEF_FIELD(21, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcMagThreshSlope, ping, 0x1903c)
DEF_NAMESPACE_REG(FrameStitch_LmMcMagThreshSlope, pong, 0x30ffc)

class FrameStitch_LmMcMagThreshThresh :
      public hwreg::RegisterBase<FrameStitch_LmMcMagThreshThresh, uint32_t> {
public:
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcMagThreshThresh, ping, 0x19040)
DEF_NAMESPACE_REG(FrameStitch_LmMcMagThreshThresh, pong, 0x31000)

class FrameStitch_LmMcMag : public hwreg::RegisterBase<FrameStitch_LmMcMag, uint32_t> {
public:
    DEF_FIELD(11, 0, lm_mc_mag_thresh_offset);
    DEF_FIELD(27, 16, lm_mc_mag_lblend_thresh);
};

DEF_NAMESPACE_REG(FrameStitch_LmMcMag, ping, 0x19044)
DEF_NAMESPACE_REG(FrameStitch_LmMcMag, pong, 0x31004)

class FrameStitch_Config3 : public hwreg::RegisterBase<FrameStitch_Config3, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_wb_offset);
    // Threshold for selection of exposure mask in blending regions.
    //        Where the alpha value is above this value the shorter
    //         exposure will be indicated.
    DEF_FIELD(23, 16, exposure_mask_thresh);
};

DEF_NAMESPACE_REG(FrameStitch_Config3, ping, 0x19048)
DEF_NAMESPACE_REG(FrameStitch_Config3, pong, 0x31008)

class FrameStitch_Config4 : public hwreg::RegisterBase<FrameStitch_Config4, uint32_t> {
public:
    DEF_BIT(0, bwb_select);
    DEF_BIT(1, use_3x3_max);
    DEF_BIT(8, mcoff_mode_enable);
    // Select which L/M stitching algorithm to use.
    DEF_BIT(16, lm_alg_select);
    DEF_BIT(2, mcoff_nc_enable);
};

DEF_NAMESPACE_REG(FrameStitch_Config4, ping, 0x1904c)
DEF_NAMESPACE_REG(FrameStitch_Config4, pong, 0x3100c)

class FrameStitch_McoffMax0 :
      public hwreg::RegisterBase<FrameStitch_McoffMax0, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_l_max);
    DEF_FIELD(27, 16, mcoff_m_max);
};

DEF_NAMESPACE_REG(FrameStitch_McoffMax0, ping, 0x19050)
DEF_NAMESPACE_REG(FrameStitch_McoffMax0, pong, 0x31010)

class FrameStitch_McoffMax1 :
      public hwreg::RegisterBase<FrameStitch_McoffMax1, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_s_max);
    DEF_FIELD(27, 16, mcoff_vs_max);
};

DEF_NAMESPACE_REG(FrameStitch_McoffMax1, ping, 0x19054)
DEF_NAMESPACE_REG(FrameStitch_McoffMax1, pong, 0x31014)

class FrameStitch_McoffScaler0 :
      public hwreg::RegisterBase<FrameStitch_McoffScaler0, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_l_scaler);
    DEF_FIELD(27, 16, mcoff_lm_scaler);
};

DEF_NAMESPACE_REG(FrameStitch_McoffScaler0, ping, 0x19058)
DEF_NAMESPACE_REG(FrameStitch_McoffScaler0, pong, 0x31018)

class FrameStitch_McoffScaler1 :
      public hwreg::RegisterBase<FrameStitch_McoffScaler1, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_lms_scaler);
    DEF_FIELD(27, 16, mcoff_nc_thresh_low);
};

DEF_NAMESPACE_REG(FrameStitch_McoffScaler1, ping, 0x1905c)
DEF_NAMESPACE_REG(FrameStitch_McoffScaler1, pong, 0x3101c)

class FrameStitch_McoffNc : public hwreg::RegisterBase<FrameStitch_McoffNc, uint32_t> {
public:
    DEF_FIELD(11, 0, mcoff_nc_thresh_high);
    DEF_FIELD(27, 16, mcoff_nc_scale);
};

DEF_NAMESPACE_REG(FrameStitch_McoffNc, ping, 0x19060)
DEF_NAMESPACE_REG(FrameStitch_McoffNc, pong, 0x31020)

class Decompander0 : public hwreg::RegisterBase<Decompander0, uint32_t> {
public:
    // Frontend lookup0 enable: 0=off 1=on
    DEF_BIT(0, enable);
    // Lookup0 reflection mode for black offset region
    //   0 = Manual curve reflection
    //   1 = Automatic curve reflection
    DEF_BIT(4, offset_mode);
};

DEF_NAMESPACE_REG(Decompander0, ping, 0x19264)
DEF_NAMESPACE_REG(Decompander0, pong, 0x31224)

class Decompander1 : public hwreg::RegisterBase<Decompander1, uint32_t> {
public:
    // Frontend lookup0 enable: 0=off 1=on
    DEF_BIT(0, enable);
    // Lookup0 reflection mode for black offset region
    //   0 = Manual curve reflection
    //   1 = Automatic curve reflection
    DEF_BIT(4, offset_mode);
};

DEF_NAMESPACE_REG(Decompander1, ping, 0x19268)
DEF_NAMESPACE_REG(Decompander1, pong, 0x31228)

class DigitalGain_Gain : public hwreg::RegisterBase<DigitalGain_Gain, uint32_t> {
public:
    // Gain applied to data in 5.8 format
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(DigitalGain_Gain, ping, 0x1926c)
DEF_NAMESPACE_REG(DigitalGain_Gain, pong, 0x3122c)

class DigitalGain_Offset : public hwreg::RegisterBase<DigitalGain_Offset, uint32_t> {
public:
    // Data black level
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(DigitalGain_Offset, ping, 0x19270)
DEF_NAMESPACE_REG(DigitalGain_Offset, pong, 0x31230)

class SensorOffsetFe_Offset00 :
      public hwreg::RegisterBase<SensorOffsetFe_Offset00, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetFe_Offset00, ping, 0x19274)
DEF_NAMESPACE_REG(SensorOffsetFe_Offset00, pong, 0x31234)

class SensorOffsetFe_Offset01 :
      public hwreg::RegisterBase<SensorOffsetFe_Offset01, uint32_t> {
public:
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetFe_Offset01, ping, 0x19278)
DEF_NAMESPACE_REG(SensorOffsetFe_Offset01, pong, 0x31238)

class SensorOffsetFe_Offset10 :
      public hwreg::RegisterBase<SensorOffsetFe_Offset10, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetFe_Offset10, ping, 0x1927c)
DEF_NAMESPACE_REG(SensorOffsetFe_Offset10, pong, 0x3123c)

class SensorOffsetFe_Offset11 :
      public hwreg::RegisterBase<SensorOffsetFe_Offset11, uint32_t> {
public:
    // offset offset for color channel 11 (B)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetFe_Offset11, ping, 0x19280)
DEF_NAMESPACE_REG(SensorOffsetFe_Offset11, pong, 0x31240)

class Sqrt_BlackLevelIn : public hwreg::RegisterBase<Sqrt_BlackLevelIn, uint32_t> {
public:
    // input Data black level
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(Sqrt_BlackLevelIn, ping, 0x19284)
DEF_NAMESPACE_REG(Sqrt_BlackLevelIn, pong, 0x31244)

class Sqrt_BlackLevelOut : public hwreg::RegisterBase<Sqrt_BlackLevelOut, uint32_t> {
public:
    // output Data black level
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Sqrt_BlackLevelOut, ping, 0x19288)
DEF_NAMESPACE_REG(Sqrt_BlackLevelOut, pong, 0x31248)

class RawFrontend_Enable : public hwreg::RegisterBase<RawFrontend_Enable, uint32_t> {
public:
    // Green equalization enable: 0=off, 1=on
    DEF_BIT(0, ge_enable);
    // Dynamic Defect Pixel enable: 0=off, 1=on
    DEF_BIT(2, dp_enable);
    // Show Defect Pixel: 0=off, 1=on
    DEF_BIT(3, show_dynamic_defect_pixel);
    // Disable detection of dark pixels
    DEF_BIT(6, dark_disable);
    // Disable detection of bright pixels
    DEF_BIT(7, bright_disable);
};

DEF_NAMESPACE_REG(RawFrontend_Enable, ping, 0x1928c)
DEF_NAMESPACE_REG(RawFrontend_Enable, pong, 0x3124c)

class RawFrontend_DebugSel : public hwreg::RegisterBase<RawFrontend_DebugSel, uint32_t> {
public:
    // Debug selection port
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(RawFrontend_DebugSel, ping, 0x19290)
DEF_NAMESPACE_REG(RawFrontend_DebugSel, pong, 0x31250)

class RawFrontend_DynamicDefectPixel0 :
      public hwreg::RegisterBase<RawFrontend_DynamicDefectPixel0, uint32_t> {
public:
    // Slope for HP Mask function
    DEF_FIELD(11, 0, dp_slope);
    // Defect pixel threshold.
    DEF_FIELD(27, 16, dp_threshold);
};

DEF_NAMESPACE_REG(RawFrontend_DynamicDefectPixel0, ping, 0x19294)
DEF_NAMESPACE_REG(RawFrontend_DynamicDefectPixel0, pong, 0x31254)

class RawFrontend_DynamicDefectPixel1 :
      public hwreg::RegisterBase<RawFrontend_DynamicDefectPixel1, uint32_t> {
public:
    //  Controls the aggressiveness of the dynamic defect pixel
    //   correction near edges.
    DEF_FIELD(15, 0, dpdev_threshold);
    //  Controls blending between non-directional and directional
    //   replacement values in dynamic defect pixel correction.
    // 0x00 Replace detected defects with non-directional replacement value
    // 0xFF Replace detected defects with directional replacement value
    DEF_FIELD(23, 16, dp_blend);
};

DEF_NAMESPACE_REG(RawFrontend_DynamicDefectPixel1, ping, 0x19298)
DEF_NAMESPACE_REG(RawFrontend_DynamicDefectPixel1, pong, 0x31258)

class RawFrontend_GreenEqualization0 :
      public hwreg::RegisterBase<RawFrontend_GreenEqualization0, uint32_t> {
public:
    // Controls strength of Green equalization.  Set during calibration.
    DEF_FIELD(7, 0, ge_strength);
    // green equalization threshold
    DEF_FIELD(31, 16, ge_threshold);
};

DEF_NAMESPACE_REG(RawFrontend_GreenEqualization0, ping, 0x1929c)
DEF_NAMESPACE_REG(RawFrontend_GreenEqualization0, pong, 0x3125c)

class RawFrontend_GreenEqualization1 :
      public hwreg::RegisterBase<RawFrontend_GreenEqualization1, uint32_t> {
public:
    // Slope for GE Mask function
    DEF_FIELD(11, 0, ge_slope);
    // Controls the sensitivity of green equalization to edges.
    DEF_FIELD(23, 16, ge_sens);
};

DEF_NAMESPACE_REG(RawFrontend_GreenEqualization1, ping, 0x192a0)
DEF_NAMESPACE_REG(RawFrontend_GreenEqualization1, pong, 0x31260)

class RawFrontend_Misc : public hwreg::RegisterBase<RawFrontend_Misc, uint32_t> {
public:
    //  Controls the directional nature of the dynamic defect pixel
    //   correction near edges..
    DEF_FIELD(15, 0, line_thresh);
    // Manual override of noise estimation
    DEF_FIELD(31, 16, sigma_in);
};

DEF_NAMESPACE_REG(RawFrontend_Misc, ping, 0x192a4)
DEF_NAMESPACE_REG(RawFrontend_Misc, pong, 0x31264)

class RawFrontend_Thresh : public hwreg::RegisterBase<RawFrontend_Thresh, uint32_t> {
public:
    // Noise threshold for short exposure data
    DEF_FIELD(7, 0, thresh_short);
    // Noise threshold for long exposure data
    DEF_FIELD(15, 8, thresh_long);
};

DEF_NAMESPACE_REG(RawFrontend_Thresh, ping, 0x192a8)
DEF_NAMESPACE_REG(RawFrontend_Thresh, pong, 0x31268)

class RawFrontendNp_ExpThresh :
      public hwreg::RegisterBase<RawFrontendNp_ExpThresh, uint32_t> {
public:
    // Threshold for determining long/short exposure data
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(RawFrontendNp_ExpThresh, ping, 0x192ac)
DEF_NAMESPACE_REG(RawFrontendNp_ExpThresh, pong, 0x3126c)

class RawFrontendNp_Ratio : public hwreg::RegisterBase<RawFrontendNp_Ratio, uint32_t> {
public:
    // Multiplier applied to short exposure data for noise profile calculation
    DEF_FIELD(7, 0, short_ratio);
    // Multiplier applied to long exposure data for noise profile calculation
    DEF_FIELD(15, 8, long_ratio);
};

DEF_NAMESPACE_REG(RawFrontendNp_Ratio, ping, 0x192b0)
DEF_NAMESPACE_REG(RawFrontendNp_Ratio, pong, 0x31270)

class RawFrontendNp_NpOff : public hwreg::RegisterBase<RawFrontendNp_NpOff, uint32_t> {
public:
    // Noise profile black level offset
    DEF_FIELD(6, 0, np_off);
    // Defines how values below black level are obtained.
    //   0: Repeat the first table entry.
    //   1: Reflect the noise profile curve below black level.
    DEF_BIT(8, np_off_reflect);
};

DEF_NAMESPACE_REG(RawFrontendNp_NpOff, ping, 0x192b4)
DEF_NAMESPACE_REG(RawFrontendNp_NpOff, pong, 0x31274)

class DefectPixel_PointerReset :
      public hwreg::RegisterBase<DefectPixel_PointerReset, uint32_t> {
public:
    //  Reset static defect-pixel table pointer each frame - set this
    //   when defect-pixel table has been written from mcu
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(DefectPixel_PointerReset, ping, 0x19338)
DEF_NAMESPACE_REG(DefectPixel_PointerReset, pong, 0x312f8)

class DefectPixel_Config0 : public hwreg::RegisterBase<DefectPixel_Config0, uint32_t> {
public:
    //  For debug purposes.  Show reference values which are compared
    //   with actual values to detect bad pixels
    DEF_BIT(0, show_reference);
    // Correction enable: 0=off 1=on
    DEF_BIT(1, correction_enable);
    // Show which pixels have been detected as bad
    DEF_BIT(2, show_static_defect_pixels);
    // Starts detection
    DEF_BIT(3, detection_enable);
};

DEF_NAMESPACE_REG(DefectPixel_Config0, ping, 0x1933c)
DEF_NAMESPACE_REG(DefectPixel_Config0, pong, 0x312fc)

class DefectPixel_Config1 : public hwreg::RegisterBase<DefectPixel_Config1, uint32_t> {
public:
    // Number of defect-pixels detected
    DEF_FIELD(12, 1, defect_pixel_count);
    // Address of first defect-pixel in defect-pixel store
    DEF_FIELD(27, 16, table_start);
    // Table overflow flag
    DEF_BIT(0, overflow);
};

DEF_NAMESPACE_REG(DefectPixel_Config1, ping, 0x19340)
DEF_NAMESPACE_REG(DefectPixel_Config1, pong, 0x31300)

class DefectPixel_DefectPixelCountIn :
      public hwreg::RegisterBase<DefectPixel_DefectPixelCountIn, uint32_t> {
public:
    // Number of defect-pixels in the written table
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DefectPixel_DefectPixelCountIn, ping, 0x19344)
DEF_NAMESPACE_REG(DefectPixel_DefectPixelCountIn, pong, 0x31304)

class Sinter_Enable : public hwreg::RegisterBase<Sinter_Enable, uint32_t> {
public:
    // For debug purposes only. Set to zero for normal operation
    DEF_FIELD(1, 0, view_filter);
    //  For debug purposes only. Set to 3 for normal operation (0=Use
    //   filter 0 only, 1=Use filters 0 and 2 only, 2=Use filters 0, 2
    //   and 4 only, 3=Use all filters)
    DEF_FIELD(3, 2, scale_mode);
    // Sinter enable: 0=off 1=on
    DEF_BIT(4, enable);
    // Sinter filter fine tuning.  Should not be modified from suggested values.
    DEF_BIT(5, filter_select);
    // Select intensity filter.  Should not be modified from suggested values.
    DEF_BIT(6, int_select);
    //  Adjusts sinter strength radially from center to compensate for
    //   Lens shading correction.
    //     enable: 0=off, 1=on
    DEF_BIT(7, rm_enable);
};

DEF_NAMESPACE_REG(Sinter_Enable, ping, 0x19348)
DEF_NAMESPACE_REG(Sinter_Enable, pong, 0x31308)

class Sinter_Config : public hwreg::RegisterBase<Sinter_Config, uint32_t> {
public:
    // Intensity blending with mosaic raw
    DEF_FIELD(3, 0, int_config);
    // This config is only valid fr sinter3
    //     Enables (1) or disables (0) the NLM filter
    DEF_BIT(4, nlm_en);
    // This config is only valid fr sinter3
    //     Enables (1) or disables (0) nonlinear weight generation
    DEF_BIT(5, nonlinear_wkgen);
};

DEF_NAMESPACE_REG(Sinter_Config, ping, 0x1934c)
DEF_NAMESPACE_REG(Sinter_Config, pong, 0x3130c)

class Sinter_SadFiltThresh : public hwreg::RegisterBase<Sinter_SadFiltThresh, uint32_t> {
public:
    // Block match difference filtering threshold
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(Sinter_SadFiltThresh, ping, 0x19350)
DEF_NAMESPACE_REG(Sinter_SadFiltThresh, pong, 0x31310)

class Sinter_RmCenter : public hwreg::RegisterBase<Sinter_RmCenter, uint32_t> {
public:
    // Center x coordinate of shading map
    DEF_FIELD(15, 0, rm_center_x);
    // Center y coordinate of shading map
    DEF_FIELD(31, 16, rm_center_y);
};

DEF_NAMESPACE_REG(Sinter_RmCenter, ping, 0x19354)
DEF_NAMESPACE_REG(Sinter_RmCenter, pong, 0x31314)

class Sinter_RmOffCenterMult :
      public hwreg::RegisterBase<Sinter_RmOffCenterMult, uint32_t> {
public:
    //  Normalizing factor which scales the radial table to the edge of
    //   the image.
    //    Calculated as 2^31/R^2 where R is the furthest distance from
    //     the center coordinate to the edge of the image in pixels.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Sinter_RmOffCenterMult, ping, 0x19358)
DEF_NAMESPACE_REG(Sinter_RmOffCenterMult, pong, 0x31318)

class Sinter_HorizontalThresh :
      public hwreg::RegisterBase<Sinter_HorizontalThresh, uint32_t> {
public:
    // Noise threshold for high horizontal spatial frequencies
    DEF_FIELD(7, 0, thresh_0h);
    // Noise threshold for high horizontal spatial frequencies
    DEF_FIELD(15, 8, thresh_1h);
    // Noise threshold for low horizontal spatial frequencies
    DEF_FIELD(23, 16, thresh_2h);
    // Noise threshold for low horizontal spatial frequencies
    DEF_FIELD(31, 24, thresh_4h);
};

DEF_NAMESPACE_REG(Sinter_HorizontalThresh, ping, 0x1935c)
DEF_NAMESPACE_REG(Sinter_HorizontalThresh, pong, 0x3131c)

class Sinter_VerticalThresh :
      public hwreg::RegisterBase<Sinter_VerticalThresh, uint32_t> {
public:
    // Noise threshold for high vertical spatial frequencies
    DEF_FIELD(7, 0, thresh_0v);
    // Noise threshold for high vertical spatial frequencies
    DEF_FIELD(15, 8, thresh_1v);
    // Noise threshold for low vertical spatial frequencies
    DEF_FIELD(23, 16, thresh_2v);
    // Noise threshold for low vertical spatial frequencies
    DEF_FIELD(31, 24, thresh_4v);
};

DEF_NAMESPACE_REG(Sinter_VerticalThresh, ping, 0x19360)
DEF_NAMESPACE_REG(Sinter_VerticalThresh, pong, 0x31320)

class Sinter_Strength : public hwreg::RegisterBase<Sinter_Strength, uint32_t> {
public:
    // Unused - no effect
    DEF_FIELD(7, 0, strength_0);
    // Noise reduction effect for high spatial frequencies
    DEF_FIELD(15, 8, strength_1);
    // Unused - no effect
    DEF_FIELD(23, 16, strength_2);
    // Noise reduction effect for low spatial frequencies
    DEF_FIELD(31, 24, strength_4);
};

DEF_NAMESPACE_REG(Sinter_Strength, ping, 0x19364)
DEF_NAMESPACE_REG(Sinter_Strength, pong, 0x31324)

class SinterNoiseProfile_Config :
      public hwreg::RegisterBase<SinterNoiseProfile_Config, uint32_t> {
public:
    // A global offset that will be added to each of the hlog... values above..
    DEF_FIELD(15, 8, global_offset);
    //  1 = use LUT data    0 = use exposure mask provided by Frame
    //       stitching or threshold
    DEF_BIT(0, use_lut);
    // 1 = use exposure mask provided by Frame stitching or threshold
    DEF_BIT(1, use_exp_mask);
    //  Specifies how to deal with data below black level. 0: Clip to
    //   zero, 1: Reflect.
    DEF_BIT(2, black_reflect);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_Config, ping, 0x19368)
DEF_NAMESPACE_REG(SinterNoiseProfile_Config, pong, 0x31328)

class SinterNoiseProfile_BlackLevel :
      public hwreg::RegisterBase<SinterNoiseProfile_BlackLevel, uint32_t> {
public:
    // Black level offset for Mode 0
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_BlackLevel, ping, 0x1936c)
DEF_NAMESPACE_REG(SinterNoiseProfile_BlackLevel, pong, 0x3132c)

class SinterNoiseProfile_Thresh1 :
      public hwreg::RegisterBase<SinterNoiseProfile_Thresh1, uint32_t> {
public:
    //  Exposure thresholds. Used to determine which exposure generated
    //   the current pixel.     Pixels with a value greater than or equal
    //   to a given threshold will be deemed to have been generated by
    //   the shorter exposure.     Pixels with a value less than a given
    //   threshold will be deemed to have been generated by the longer
    //   exposure.
    //   E.G. Where 4 exposures are used:       VS >= Thresh 3 > S >=
    //    Thresh 2 > M >= Thresh 1 > L
    //     For 3 exposures set Thresh 1 to 0     For 2 exposures set
    //      Thresh 1 and Thresh 2 to 0     For 1 exposures set all
    //      exposure thresholds to 0
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh1, ping, 0x19370)
DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh1, pong, 0x31330)

class SinterNoiseProfile_Thresh2 :
      public hwreg::RegisterBase<SinterNoiseProfile_Thresh2, uint32_t> {
public:
    // See above.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh2, ping, 0x19374)
DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh2, pong, 0x31334)

class SinterNoiseProfile_Thresh3 :
      public hwreg::RegisterBase<SinterNoiseProfile_Thresh3, uint32_t> {
public:
    // See above.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh3, ping, 0x19378)
DEF_NAMESPACE_REG(SinterNoiseProfile_Thresh3, pong, 0x31338)

class SinterNoiseProfile_NoiseLevel :
      public hwreg::RegisterBase<SinterNoiseProfile_NoiseLevel, uint32_t> {
public:
    // Noise level of VS exposure
    DEF_FIELD(7, 0, noise_level_0);
    // Noise level of S exposure
    DEF_FIELD(15, 8, noise_level_1);
    // Noise level of M exposure
    DEF_FIELD(23, 16, noise_level_2);
    // Noise level of L exposure
    DEF_FIELD(31, 24, noise_level_3);
};

DEF_NAMESPACE_REG(SinterNoiseProfile_NoiseLevel, ping, 0x1937c)
DEF_NAMESPACE_REG(SinterNoiseProfile_NoiseLevel, pong, 0x3133c)

class Temper_Config0 : public hwreg::RegisterBase<Temper_Config0, uint32_t> {
public:
    // Temper enable: 0=off 1=on
    DEF_BIT(0, enable);
    // 0: 0=Temper3 mode 1=Temper2 mode
    DEF_BIT(1, temper2_mode);
};

DEF_NAMESPACE_REG(Temper_Config0, ping, 0x1aa1c)
DEF_NAMESPACE_REG(Temper_Config0, pong, 0x329dc)

class Temper_Config1 : public hwreg::RegisterBase<Temper_Config1, uint32_t> {
public:
    // Extra output delay: 0=normal output 1=delayed by 1 frame
    DEF_BIT(0, frame_delay);
    // 1=Normal operation, 0=disable logarithmic weighting function for debug
    DEF_BIT(1, log_enable);
    // 0=Normal operation, 1=output alpha channel for debug
    DEF_BIT(2, show_alpha);
    // 0=Normal operation, 1=output alpha channel for debug
    DEF_BIT(3, show_alphaab);
    //  Debug mixer select(Only active when Temper disabled): 0=Input
    //   video stream, 1=Frame buffer video stream
    DEF_BIT(4, mixer_select);
};

DEF_NAMESPACE_REG(Temper_Config1, ping, 0x1aa20)
DEF_NAMESPACE_REG(Temper_Config1, pong, 0x329e0)

class Temper_Config2 : public hwreg::RegisterBase<Temper_Config2, uint32_t> {
public:
    //  Controls length of filter history. Low values result in longer
    //   history and stronger temporal filtering.
    DEF_FIELD(3, 0, recursion_limit);
    DEF_FIELD(15, 8, delta);
};

DEF_NAMESPACE_REG(Temper_Config2, ping, 0x1aa24)
DEF_NAMESPACE_REG(Temper_Config2, pong, 0x329e4)

class TemperNoiseProfile_ : public hwreg::RegisterBase<TemperNoiseProfile_, uint32_t> {
public:
    // A global offset that will be added to each of the hlog... values above..
    DEF_FIELD(15, 8, global_offset);
    //  1 = use LUT data    0 = use exposure mask provided by Frame
    //       stitching or threshold
    DEF_BIT(0, use_lut);
    // 1 = use exposure mask provided by Frame stitching or threshold
    DEF_BIT(1, use_exp_mask);
    //  Specifies how to deal with data below black level. 0: Clip to
    //   zero, 1: Reflect.
    DEF_BIT(2, black_reflect);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_, ping, 0x1aa28)
DEF_NAMESPACE_REG(TemperNoiseProfile_, pong, 0x329e8)

class TemperNoiseProfile_BlackLevel :
      public hwreg::RegisterBase<TemperNoiseProfile_BlackLevel, uint32_t> {
public:
    // Black level offset for Mode 0
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_BlackLevel, ping, 0x1aa2c)
DEF_NAMESPACE_REG(TemperNoiseProfile_BlackLevel, pong, 0x329ec)

class TemperNoiseProfile_Thresh1 :
      public hwreg::RegisterBase<TemperNoiseProfile_Thresh1, uint32_t> {
public:
    //  Exposure thresholds. Used to determine which exposure generated
    //   the current pixel.     Pixels with a value greater than or equal
    //   to a given threshold will be deemed to have been generated by
    //   the shorter exposure.     Pixels with a value less than a given
    //   threshold will be deemed to have been generated by the longer
    //   exposure.
    //   E.G. Where 4 exposures are used:       VS >= Thresh 3 > S >=
    //    Thresh 2 > M >= Thresh 1 > L
    //     For 3 exposures set Thresh 1 to 0     For 2 exposures set
    //      Thresh 1 and Thresh 2 to 0     For 1 exposures set all
    //      exposure thresholds to 0
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh1, ping, 0x1aa30)
DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh1, pong, 0x329f0)

class TemperNoiseProfile_Thresh2 :
      public hwreg::RegisterBase<TemperNoiseProfile_Thresh2, uint32_t> {
public:
    // See above.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh2, ping, 0x1aa34)
DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh2, pong, 0x329f4)

class TemperNoiseProfile_Thresh3 :
      public hwreg::RegisterBase<TemperNoiseProfile_Thresh3, uint32_t> {
public:
    // See above.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh3, ping, 0x1aa38)
DEF_NAMESPACE_REG(TemperNoiseProfile_Thresh3, pong, 0x329f8)

class TemperNoiseProfile_NoiseLevel :
      public hwreg::RegisterBase<TemperNoiseProfile_NoiseLevel, uint32_t> {
public:
    // Noise level of VS exposure
    DEF_FIELD(7, 0, noise_level_0);
    // Noise level of S exposure
    DEF_FIELD(15, 8, noise_level_1);
    // Noise level of M exposure
    DEF_FIELD(23, 16, noise_level_2);
    // Noise level of L exposure
    DEF_FIELD(31, 24, noise_level_3);
};

DEF_NAMESPACE_REG(TemperNoiseProfile_NoiseLevel, ping, 0x1aa3c)
DEF_NAMESPACE_REG(TemperNoiseProfile_NoiseLevel, pong, 0x329fc)

class TemperDma_FrameDma : public hwreg::RegisterBase<TemperDma_FrameDma, uint32_t> {
public:
    // This must be set to 1 only in Temper-3 mode
    DEF_BIT(0, frame_write_on_msb_dma);
    // This must be set to 1 whenever Temper (either T2 or T3 mode) is enabled
    DEF_BIT(1, frame_write_on_lsb_dma);
    // This must be set to 1 only in Temper-3 mode
    DEF_BIT(2, frame_read_on_msb_dma);
    // This must be set to 1 whenever Temper (either T2 or T3 mode) is enabled
    DEF_BIT(3, frame_read_on_lsb_dma);
    // 0: 16bit valid data
    //     1: upto 12 bit valid data, MSB aligened to 16 bit
    DEF_BIT(10, temper_dw);
};

DEF_NAMESPACE_REG(TemperDma_FrameDma, ping, 0x1ab78)
DEF_NAMESPACE_REG(TemperDma_FrameDma, pong, 0x32b38)

class TemperDma_Format : public hwreg::RegisterBase<TemperDma_Format, uint32_t> {
public:
    // 20: for 16bit data both in T3 and T2 modes
    // 6 : for 12bit data both in T3 and T2 modes
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_Format, ping, 0x1ab7c)
DEF_NAMESPACE_REG(TemperDma_Format, pong, 0x32b3c)

class TemperDma_BlkStatus : public hwreg::RegisterBase<TemperDma_BlkStatus, uint32_t> {
public:
    // The bits are defined as follows:
    //   0     Write FIFO Fail (Full)
    //   1     Write FIFO Fail (Empty)
    //   2     Read FIFO Fail (Full)
    //   3     Read FIFO Fail (Empty)
    //   4     Pack Fail (Overflow)
    //   5     Unpack Fail (Overflow)
    //   6     Writer fail (Active Width)
    //   7     Writer fail (Active Height)
    //   8     Writer fail (Interline blanking)
    //   9     Writer fail (Interframe blanking)
    //   10    Reader fail (Active Width)
    //   11    Reader fail (Active Height)
    //   12    Reader fail (Interline blanking)
    //   13    Reader fail (Interframe blanking)
    //   14    0
    //   15    0
    //   16    Writer fail (A resp)
    //   17    Writer fail (AW wait)
    //   18    Writer fail (W wait)
    //   19    Writer fail (Outstanding Transactions)
    //   20    Reader fail (AR wait)
    //   21    Reader fail (R resp)
    //   22    Reader fail (Oustanding Transfers)
    //   23    0
    //   24    intw_fail_user_intfc_sig
    //   25    intr_fail_user_intfc_sig
    //   26    0
    //   27    0
    //   28    0
    //   29    0
    //   30    0
    //   31    0
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_BlkStatus, ping, 0x1ab80)
DEF_NAMESPACE_REG(TemperDma_BlkStatus, pong, 0x32b40)

class TemperDma_MsbBankBaseWriter :
      public hwreg::RegisterBase<TemperDma_MsbBankBaseWriter, uint32_t> {
public:
    //  base address for frame buffer, should be word-aligned. This is
    //   used only in 16bit temper3 mode.
    //      In 16bit temper3 mode, each 40 bit temper data (32bit
    //       data+8bit meta data) is split into 2 chunks and each
    //      is stored in one of the buffers. The MSB part is stored into
    //       this buffer
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_MsbBankBaseWriter, ping, 0x1ab84)
DEF_NAMESPACE_REG(TemperDma_MsbBankBaseWriter, pong, 0x32b44)

class TemperDma_LsbBankBaseWriter :
      public hwreg::RegisterBase<TemperDma_LsbBankBaseWriter, uint32_t> {
public:
    //  base address for frame buffer, should be word-aligned. This is
    //   used all the times temper is used..
    //      In 16bit temper3 mode, each 40 bit temper data (32bit
    //       data+8bit meta data) is split into 2 chunks and each
    //      is stored in one of the buffers. The LSB part is stored into
    //       this buffer.
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_LsbBankBaseWriter, ping, 0x1ab88)
DEF_NAMESPACE_REG(TemperDma_LsbBankBaseWriter, pong, 0x32b48)

class TemperDma_MsbBankBaseReader :
      public hwreg::RegisterBase<TemperDma_MsbBankBaseReader, uint32_t> {
public:
    //  base address for frame buffer, should be word-aligned. This is
    //   used only in 16bit temper3 mode.
    //      In 16bit temper3 mode, each 40 bit temper data (32bit
    //       data+8bit meta data) is split into 2 chunks and each
    //      is stored in one of the buffers. The MSB part is stored into
    //       this buffer
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_MsbBankBaseReader, ping, 0x1ab8c)
DEF_NAMESPACE_REG(TemperDma_MsbBankBaseReader, pong, 0x32b4c)

class TemperDma_LsbBankBaseReader :
      public hwreg::RegisterBase<TemperDma_LsbBankBaseReader, uint32_t> {
public:
    //  base address for frame buffer, should be word-aligned. This is
    //   used all the times temper is used..
    //      In 16bit temper3 mode, each 40 bit temper data (32bit
    //       data+8bit meta data) is split into 2 chunks and each
    //      is stored in one of the buffers. The LSB part is stored into
    //       this buffer.
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_LsbBankBaseReader, ping, 0x1ab90)
DEF_NAMESPACE_REG(TemperDma_LsbBankBaseReader, pong, 0x32b50)

class TemperDma_LineOffset : public hwreg::RegisterBase<TemperDma_LineOffset, uint32_t> {
public:
    //  Indicates the offset in bytes from the start of one line to the
    //   next line.
    //      This value should be equal to or larger than one line of
    //       image data and should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_LineOffset, ping, 0x1ab94)
DEF_NAMESPACE_REG(TemperDma_LineOffset, pong, 0x32b54)

class TemperDma_LinetickEol :
      public hwreg::RegisterBase<TemperDma_LinetickEol, uint32_t> {
public:
    //  linetick start/end of line control. 0 = use start of line, 1 =
    //   use end of line
    DEF_BIT(1, value);
};

DEF_NAMESPACE_REG(TemperDma_LinetickEol, ping, 0x1ab98)
DEF_NAMESPACE_REG(TemperDma_LinetickEol, pong, 0x32b58)

class TemperDma_Config : public hwreg::RegisterBase<TemperDma_Config, uint32_t> {
public:
    //  number of lines to write from base address before wrapping back
    //   to base address
    DEF_FIELD(15, 0, lines_wrapped);
    // max fill level of fifo to allow
    DEF_FIELD(31, 16, fifo_maxfill);
};

DEF_NAMESPACE_REG(TemperDma_Config, ping, 0x1ab9c)
DEF_NAMESPACE_REG(TemperDma_Config, pong, 0x32b5c)

class TemperDma_Linetick : public hwreg::RegisterBase<TemperDma_Linetick, uint32_t> {
public:
    // line number of first linetick. 0  = no linetick
    DEF_FIELD(15, 0, linetick_first);
    // line number of first linetick. 0 = no repeat
    DEF_FIELD(31, 16, linetick_repeat);
};

DEF_NAMESPACE_REG(TemperDma_Linetick, ping, 0x1aba0)
DEF_NAMESPACE_REG(TemperDma_Linetick, pong, 0x32b60)

class TemperDma_LinetickDelay :
      public hwreg::RegisterBase<TemperDma_LinetickDelay, uint32_t> {
public:
    // linetick delay in vcke cycles to add
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(TemperDma_LinetickDelay, ping, 0x1aba4)
DEF_NAMESPACE_REG(TemperDma_LinetickDelay, pong, 0x32b64)

class TemperDma_AxiWriter : public hwreg::RegisterBase<TemperDma_AxiWriter, uint32_t> {
public:
    // value to send for awid, wid and expected on bid.
    DEF_FIELD(3, 0, msb_writer_axi_id_value);
    // value to send for awid, wid and expected on bid.
    DEF_FIELD(7, 4, lsb_writer_axi_id_value);
    //  memory boundary that splits bursts:
    //   0=2Transfers,1=4Transfers,2=8Transfers,3=16Transfers. (for
    //   axi_data_w=128,  16transfers=256Bytes). Good default = 11
    DEF_FIELD(10, 9, writer_axi_burstsplit);
    // value to send for awcache. Good default = 1111
    DEF_FIELD(14, 11, writer_axi_cache_value);
    //  max outstanding write transactions (bursts) allowed. zero means
    //   no maximum(uses internal limit of 2048).
    DEF_FIELD(23, 16, writer_axi_maxostand);
    //  max value to use for awlen (axi burst length). 0000= max 1
    //   transfer/burst , upto 1111= max 16 transfers/burst
    DEF_FIELD(27, 24, writer_axi_max_awlen);
    //  0= static value (axi_id_value) for awid/wid, 1 = incrementing
    //      value per transaction for awid/wid wrapping to 0 after
    //      axi_id_value
    DEF_BIT(8, writer_axi_id_multi);
    //  active high, enables posting of pagewarm dummy writes to SMMU for
    //   early page translation of upcomming 4K pages.
    //   Recommend SMMU has min 8 page cache to avoid translation miss.
    //    Pagewarms are posted as dummy writes with wstrb= 0
    DEF_BIT(28, writer_pagewarm_on);
};

DEF_NAMESPACE_REG(TemperDma_AxiWriter, ping, 0x1aba8)
DEF_NAMESPACE_REG(TemperDma_AxiWriter, pong, 0x32b68)

class TemperDma_AxiReader : public hwreg::RegisterBase<TemperDma_AxiReader, uint32_t> {
public:
    // value to send for awid, wid and expected on bid. Good default = 0000
    DEF_FIELD(3, 0, msb_reader_axi_id_value);
    // value to send for awid, wid and expected on bid. Good default = 0000
    DEF_FIELD(7, 4, lsb_reader_axi_id_value);
    //  memory boundary that splits bursts:
    //   0=2Transfers,1=4Transfers,2=8Transfers,3=16Transfers. (for
    //   axi_data_w=128,  16transfers=256Bytes). Good default = 11
    DEF_FIELD(10, 9, reader_axi_burstsplit);
    // value to send for awcache. Good default = 1111
    DEF_FIELD(14, 11, reader_axi_cache_value);
    //  max outstanding write transactions (bursts) allowed. zero means
    //   no maximum(uses internal limit of 2048).
    DEF_FIELD(23, 16, reader_axi_maxostand);
    //  max value to use for awlen (axi burst length). 0000= max 1
    //   transfer/burst , upto 1111= max 16 transfers/burst
    DEF_FIELD(27, 24, reader_axi_max_arlen);
    //  active high, enables posting of pagewarm dummy writes to SMMU for
    //   early page translation of upcomming 4K pages.
    //   Recommend SMMU has min 8 page cache to avoid translation miss.
    //    Pagewarms are posted as dummy writes with wstrb= 0
    DEF_BIT(28, reader_pagewarm_on);
};

DEF_NAMESPACE_REG(TemperDma_AxiReader, ping, 0x1abac)
DEF_NAMESPACE_REG(TemperDma_AxiReader, pong, 0x32b6c)

class ChromaticAberrationCorrection_Config :
      public hwreg::RegisterBase<ChromaticAberrationCorrection_Config, uint32_t> {
public:
    //  extra shift of mesh data: 00- no shift, 01- shift left by 1, ...,
    //   11- shift left by 3, used to increase the range at cost of
    //   accuracy
    DEF_FIELD(5, 4, mesh_scale);
    //  module enable, if 0 the data_i(dw*(kh-1)/2+dw-1 downto
    //   dw*(kh-1)/2) is presented at data_o after pipeline length
    DEF_BIT(0, enable);
};

DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Config, ping, 0x1abb0)
DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Config, pong, 0x32b70)

class ChromaticAberrationCorrection_Mesh :
      public hwreg::RegisterBase<ChromaticAberrationCorrection_Mesh, uint32_t> {
public:
    // number of tiles across. Maximum supported mesh width is 64.
    DEF_FIELD(6, 0, mesh_width);
    //  number of tiles vertically. Maximum supported mesh height is 64
    //   for RGGB sensor and 42 for RGBIr sensors.
    DEF_FIELD(22, 16, mesh_height);
};

DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Mesh, ping, 0x1abb4)
DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Mesh, pong, 0x32b74)

class ChromaticAberrationCorrection_Offset :
      public hwreg::RegisterBase<ChromaticAberrationCorrection_Offset, uint32_t> {
public:
    //  offset between lines of tiles, can differ from mesh_width, but
    //   its safe to keep same as mesh width
    DEF_FIELD(12, 0, line_offset);
    // offset between colour planes, can differ from line_offset*mesh_height
    DEF_FIELD(28, 16, plane_offset);
};

DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Offset, ping, 0x1abb8)
DEF_NAMESPACE_REG(ChromaticAberrationCorrection_Offset, pong, 0x32b78)

class ChromaticAberrationCorrection_MeshReload :
      public hwreg::RegisterBase<ChromaticAberrationCorrection_MeshReload, uint32_t> {
public:
    //  0-1 triggers mesh and filter coefficient reload in the internal
    //       cache. Used after RAM is updated by CPU Chromatic Aberration
    //       correction module
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(ChromaticAberrationCorrection_MeshReload, ping, 0x1abbc)
DEF_NAMESPACE_REG(ChromaticAberrationCorrection_MeshReload, pong, 0x32b7c)

class SquareBe_BlackLevelIn :
      public hwreg::RegisterBase<SquareBe_BlackLevelIn, uint32_t> {
public:
    // input Data black level
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(SquareBe_BlackLevelIn, ping, 0x1abc0)
DEF_NAMESPACE_REG(SquareBe_BlackLevelIn, pong, 0x32b80)

class SquareBe_BlackLevelOut :
      public hwreg::RegisterBase<SquareBe_BlackLevelOut, uint32_t> {
public:
    // output Data black level
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SquareBe_BlackLevelOut, ping, 0x1abc4)
DEF_NAMESPACE_REG(SquareBe_BlackLevelOut, pong, 0x32b84)

class SensorOffsetPreShading_Offset00 :
      public hwreg::RegisterBase<SensorOffsetPreShading_Offset00, uint32_t> {
public:
    // offset offset for color channel 00 (R)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset00, ping, 0x1abc8)
DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset00, pong, 0x32b88)

class SensorOffsetPreShading_Offset01 :
      public hwreg::RegisterBase<SensorOffsetPreShading_Offset01, uint32_t> {
public:
    // offset offset for color channel 01 (Gr)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset01, ping, 0x1abcc)
DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset01, pong, 0x32b8c)

class SensorOffsetPreShading_Offset10 :
      public hwreg::RegisterBase<SensorOffsetPreShading_Offset10, uint32_t> {
public:
    // offset offset for color channel 10 (Gb)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset10, ping, 0x1abd0)
DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset10, pong, 0x32b90)

class SensorOffsetPreShading_Offset11 :
      public hwreg::RegisterBase<SensorOffsetPreShading_Offset11, uint32_t> {
public:
    // offset offset for color channel 11 (B)
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset11, ping, 0x1abd4)
DEF_NAMESPACE_REG(SensorOffsetPreShading_Offset11, pong, 0x32b94)

class RadialShading_Enable : public hwreg::RegisterBase<RadialShading_Enable, uint32_t> {
public:
    // Lens shading correction enable: 0=off, 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(RadialShading_Enable, ping, 0x1abd8)
DEF_NAMESPACE_REG(RadialShading_Enable, pong, 0x32b98)

class RadialShading_CenterR :
      public hwreg::RegisterBase<RadialShading_CenterR, uint32_t> {
public:
    // Center x coordinate of the red shading map
    DEF_FIELD(15, 0, centerr_x);
    // Center y coordinate of the red shading map
    DEF_FIELD(31, 16, centerr_y);
};

DEF_NAMESPACE_REG(RadialShading_CenterR, ping, 0x1abdc)
DEF_NAMESPACE_REG(RadialShading_CenterR, pong, 0x32b9c)

class RadialShading_CenterG :
      public hwreg::RegisterBase<RadialShading_CenterG, uint32_t> {
public:
    // Center x coordinate of the green shading map
    DEF_FIELD(15, 0, centerg_x);
    // Center y coordinate of the green shading map
    DEF_FIELD(31, 16, centerg_y);
};

DEF_NAMESPACE_REG(RadialShading_CenterG, ping, 0x1abe0)
DEF_NAMESPACE_REG(RadialShading_CenterG, pong, 0x32ba0)

class RadialShading_CenterB :
      public hwreg::RegisterBase<RadialShading_CenterB, uint32_t> {
public:
    // Center x coordinate of the blue shading map
    DEF_FIELD(15, 0, centerb_x);
    // Center y coordinate of the blue shading map
    DEF_FIELD(31, 16, centerb_y);
};

DEF_NAMESPACE_REG(RadialShading_CenterB, ping, 0x1abe4)
DEF_NAMESPACE_REG(RadialShading_CenterB, pong, 0x32ba4)

class RadialShading_CenterIr :
      public hwreg::RegisterBase<RadialShading_CenterIr, uint32_t> {
public:
    // Center x coordinate of the IR shading map
    DEF_FIELD(15, 0, centerir_x);
    // Center y coordinate of the IR shading map
    DEF_FIELD(31, 16, centerir_y);
};

DEF_NAMESPACE_REG(RadialShading_CenterIr, ping, 0x1abe8)
DEF_NAMESPACE_REG(RadialShading_CenterIr, pong, 0x32ba8)

class RadialShading_OffCenterMultr :
      public hwreg::RegisterBase<RadialShading_OffCenterMultr, uint32_t> {
public:
    //  Normalizing X factor which scales the Red radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(15, 0, off_center_multrx);
    //  Normalizing Y factor which scales the Red radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(31, 16, off_center_multry);
};

DEF_NAMESPACE_REG(RadialShading_OffCenterMultr, ping, 0x1abec)
DEF_NAMESPACE_REG(RadialShading_OffCenterMultr, pong, 0x32bac)

class RadialShading_OffCenterMultg :
      public hwreg::RegisterBase<RadialShading_OffCenterMultg, uint32_t> {
public:
    //  Normalizing X factor which scales the green radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(15, 0, off_center_multgx);
    //  Normalizing Y factor which scales the green radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(31, 16, off_center_multgy);
};

DEF_NAMESPACE_REG(RadialShading_OffCenterMultg, ping, 0x1abf0)
DEF_NAMESPACE_REG(RadialShading_OffCenterMultg, pong, 0x32bb0)

class RadialShading_OffCenterMultb :
      public hwreg::RegisterBase<RadialShading_OffCenterMultb, uint32_t> {
public:
    //  Normalizing X factor which scales the blue radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(15, 0, off_center_multbx);
    //  Normalizing Y factor which scales the blue radial table to the
    //   edge of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(31, 16, off_center_multby);
};

DEF_NAMESPACE_REG(RadialShading_OffCenterMultb, ping, 0x1abf4)
DEF_NAMESPACE_REG(RadialShading_OffCenterMultb, pong, 0x32bb4)

class RadialShading_OffCenterMultir :
      public hwreg::RegisterBase<RadialShading_OffCenterMultir, uint32_t> {
public:
    //  Normalizing X factor which scales the Ir radial table to the edge
    //   of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(15, 0, off_center_multirx);
    //  Normalizing Y factor which scales the Ir radial table to the edge
    //   of the image.
    //  Calculated as 2^31/R^2 where R is the furthest distance from the
    //   center coordinate to the edge of the image in pixels.
    DEF_FIELD(31, 16, off_center_multiry);
};

DEF_NAMESPACE_REG(RadialShading_OffCenterMultir, ping, 0x1abf8)
DEF_NAMESPACE_REG(RadialShading_OffCenterMultir, pong, 0x32bb8)

class MeshShading_Config : public hwreg::RegisterBase<MeshShading_Config, uint32_t> {
public:
    // Selects the precision and maximal gain range of mesh shading correction
    //  Gain range:    00- 0..2; 01- 0..4; 02- 0..8; 03- 0..16; 04- 1..2;
    //   05- 1..3; 06- 1..5; 07- 1..9(float)
    DEF_FIELD(4, 2, mesh_scale);
    // Sets alpha blending between mesh shading tables.
    // 0 = no alpha blending;
    //  1=2 banks (odd/even bytes)
    //  2=4 banks (one per 8 bit lane in each dword)
    DEF_FIELD(6, 5, mesh_alpha_mode);
    //  Selects memory page for red pixels correction.  See ISP guide for
    //   further details
    DEF_FIELD(9, 8, mesh_page_r);
    //  Selects memory page for green pixels correction.  See ISP guide
    //   for further details
    DEF_FIELD(11, 10, mesh_page_g);
    //  Selects memory page for blue pixels correction.  See ISP guide
    //   for further details
    DEF_FIELD(13, 12, mesh_page_b);
    //  Selects memory page for IR pixels correction.  See ISP guide for
    //   further details
    DEF_FIELD(15, 14, mesh_page_ir);
    // Number of horizontal nodes minus 1
    DEF_FIELD(21, 16, mesh_width);
    // Number of vertical nodes minus 1
    DEF_FIELD(29, 24, mesh_height);
    // Lens shading correction enable: 0=off, 1=on
    DEF_BIT(0, enable);
    // Lens shading correction debug: 0=off, 1=on (show mesh data)
    DEF_BIT(1, mesh_show);
};

DEF_NAMESPACE_REG(MeshShading_Config, ping, 0x1abfc)
DEF_NAMESPACE_REG(MeshShading_Config, pong, 0x32bbc)

class MeshShading_MeshReload :
      public hwreg::RegisterBase<MeshShading_MeshReload, uint32_t> {
public:
    // 0-1 triggers cache reload
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(MeshShading_MeshReload, ping, 0x1ac00)
DEF_NAMESPACE_REG(MeshShading_MeshReload, pong, 0x32bc0)

class MeshShading_MeshAlphaBank :
      public hwreg::RegisterBase<MeshShading_MeshAlphaBank, uint32_t> {
public:
    //  Bank selection for R blend: 0: 0+1; 1: 1+2; 2: 2:3; 3: 3+0;
    //   4:0+2; 5: 1+3; 6,7: reserved
    DEF_FIELD(2, 0, mesh_alpha_bank_r);
    //  Bank selection for G blend: 0: 0+1; 1: 1+2; 2: 2:3; 3: 3+0;
    //   4:0+2; 5: 1+3; 6,7: reserved:
    DEF_FIELD(5, 3, mesh_alpha_bank_g);
    //  Bank selection for B blend: 0: 0+1; 1: 1+2; 2: 2:3; 3: 3+0;
    //   4:0+2; 5: 1+3; 6,7: reserved
    DEF_FIELD(8, 6, mesh_alpha_bank_b);
    //  Bank selection for Ir blend: 0: 0+1; 1: 1+2; 2: 2:3; 3: 3+0;
    //   4:0+2; 5: 1+3; 6,7: reserved
    DEF_FIELD(11, 9, mesh_alpha_bank_ir);
};

DEF_NAMESPACE_REG(MeshShading_MeshAlphaBank, ping, 0x1ac04)
DEF_NAMESPACE_REG(MeshShading_MeshAlphaBank, pong, 0x32bc4)

class MeshShading_MeshAlpha :
      public hwreg::RegisterBase<MeshShading_MeshAlpha, uint32_t> {
public:
    // Alpha blend coeff for R
    DEF_FIELD(7, 0, mesh_alpha_r);
    // Alpha blend coeff for G
    DEF_FIELD(15, 8, mesh_alpha_g);
    // Alpha blend coeff for B
    DEF_FIELD(23, 16, mesh_alpha_b);
    // Alpha blend coeff for IR
    DEF_FIELD(31, 24, mesh_alpha_ir);
};

DEF_NAMESPACE_REG(MeshShading_MeshAlpha, ping, 0x1ac08)
DEF_NAMESPACE_REG(MeshShading_MeshAlpha, pong, 0x32bc8)

class MeshShading_MeshStrength :
      public hwreg::RegisterBase<MeshShading_MeshStrength, uint32_t> {
public:
    //  Mesh strength in 4.12 format, e.g. 0 - no correction, 4096 -
    //   correction to match mesh data. Can be used to reduce shading
    //   correction based on AE.
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(MeshShading_MeshStrength, ping, 0x1ac0c)
DEF_NAMESPACE_REG(MeshShading_MeshStrength, pong, 0x32bcc)

class WhiteBalance_Gain0 : public hwreg::RegisterBase<WhiteBalance_Gain0, uint32_t> {
public:
    // Multiplier for color channel 00 (R)
    DEF_FIELD(11, 0, gain_00);
    // Multiplier for color channel 01 (Gr)
    DEF_FIELD(27, 16, gain_01);
};

DEF_NAMESPACE_REG(WhiteBalance_Gain0, ping, 0x1ac10)
DEF_NAMESPACE_REG(WhiteBalance_Gain0, pong, 0x32bd0)

class WhiteBalance_Gain1 : public hwreg::RegisterBase<WhiteBalance_Gain1, uint32_t> {
public:
    // Multiplier for color channel 10 (Gb)
    DEF_FIELD(11, 0, gain_10);
    // Multiplier for color channel 11 (B)
    DEF_FIELD(27, 16, gain_11);
};

DEF_NAMESPACE_REG(WhiteBalance_Gain1, ping, 0x1ac14)
DEF_NAMESPACE_REG(WhiteBalance_Gain1, pong, 0x32bd4)

class WhiteBalanceAexp_Gain0 :
      public hwreg::RegisterBase<WhiteBalanceAexp_Gain0, uint32_t> {
public:
    // Multiplier for color channel 00 (R)
    DEF_FIELD(11, 0, gain_00);
    // Multiplier for color channel 01 (Gr)
    DEF_FIELD(27, 16, gain_01);
};

DEF_NAMESPACE_REG(WhiteBalanceAexp_Gain0, ping, 0x1ac18)
DEF_NAMESPACE_REG(WhiteBalanceAexp_Gain0, pong, 0x32bd8)

class WhiteBalanceAexp_Gain1 :
      public hwreg::RegisterBase<WhiteBalanceAexp_Gain1, uint32_t> {
public:
    // Multiplier for color channel 10 (Gb)
    DEF_FIELD(11, 0, gain_10);
    // Multiplier for color channel 11 (B)
    DEF_FIELD(27, 16, gain_11);
};

DEF_NAMESPACE_REG(WhiteBalanceAexp_Gain1, ping, 0x1ac1c)
DEF_NAMESPACE_REG(WhiteBalanceAexp_Gain1, pong, 0x32bdc)

class IridixGain_Gain : public hwreg::RegisterBase<IridixGain_Gain, uint32_t> {
public:
    // Gain applied to data in 4.8 format
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(IridixGain_Gain, ping, 0x1ac20)
DEF_NAMESPACE_REG(IridixGain_Gain, pong, 0x32be0)

class IridixGain_Offset : public hwreg::RegisterBase<IridixGain_Offset, uint32_t> {
public:
    // Data black level
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(IridixGain_Offset, ping, 0x1ac24)
DEF_NAMESPACE_REG(IridixGain_Offset, pong, 0x32be4)

class Iridix_Enable : public hwreg::RegisterBase<Iridix_Enable, uint32_t> {
public:
    DEF_FIELD(15, 14, stat_mult_write);
    // Iridix enable: 0=off 1=on
    DEF_BIT(0, iridix_on);
    // Max Bayer Algorithm Type.
    DEF_BIT(3, max_alg_type_write);
    //  1=Ignore Black level (set to zero) in amplificator. 0=Use Black
    //     level value.
    DEF_BIT(5, black_level_amp0_write);
    // Post Gamma application 0=gain 1=data
    DEF_BIT(6, postgamma_pos_write);
    DEF_BIT(8, collect_ovl_write);
    DEF_BIT(9, collect_rnd_write);
    DEF_BIT(10, stat_norm_write);
};

DEF_NAMESPACE_REG(Iridix_Enable, ping, 0x1ac28)
DEF_NAMESPACE_REG(Iridix_Enable, pong, 0x32be8)

class Iridix_Config0 : public hwreg::RegisterBase<Iridix_Config0, uint32_t> {
public:
    // Sets the degree of spatial sensitivity of the algorithm(Irdx7F)
    DEF_FIELD(3, 0, variance_space);
    // Sets the degree of luminance sensitivity of the algorithm(Irdx7F)
    DEF_FIELD(7, 4, variance_intensity);
    //  Restricts the maximum slope (gain) which can be generated by the
    //   adaptive algorithm
    DEF_FIELD(15, 8, slope_max);
    //  Restricts the minimum slope (gain) which can be generated by the
    //   adaptive algorithm
    DEF_FIELD(23, 16, slope_min);
};

DEF_NAMESPACE_REG(Iridix_Config0, ping, 0x1ac2c)
DEF_NAMESPACE_REG(Iridix_Config0, pong, 0x32bec)

class Iridix_BlackLevel : public hwreg::RegisterBase<Iridix_BlackLevel, uint32_t> {
public:
    // Iridix black level. Values below this will not be affected by Iridix.
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(Iridix_BlackLevel, ping, 0x1ac30)
DEF_NAMESPACE_REG(Iridix_BlackLevel, pong, 0x32bf0)

class Iridix_WhiteLevel : public hwreg::RegisterBase<Iridix_WhiteLevel, uint32_t> {
public:
    // Iridix white level. Values above this will not be affected by Iridix.
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(Iridix_WhiteLevel, ping, 0x1ac34)
DEF_NAMESPACE_REG(Iridix_WhiteLevel, pong, 0x32bf4)

class Iridix_CollectionCorrection :
      public hwreg::RegisterBase<Iridix_CollectionCorrection, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(Iridix_CollectionCorrection, ping, 0x1ac38)
DEF_NAMESPACE_REG(Iridix_CollectionCorrection, pong, 0x32bf8)

class Iridix_PerceptControl :
      public hwreg::RegisterBase<Iridix_PerceptControl, uint32_t> {
public:
    //  Iridix gamma processing select: 0=pass through 1=gamma_dl 2=sqrt
    //   3=gamma_lut.
    DEF_FIELD(1, 0, fwd_percept_control);
    //  Iridix gamma processing select: 0=pass through 1=gamma_dl 2=sqrt
    //   3=gamma_lut.
    DEF_FIELD(9, 8, rev_percept_control);
    // Manual Strength value for inside of ROI
    DEF_FIELD(25, 16, strength_inroi);
};

DEF_NAMESPACE_REG(Iridix_PerceptControl, ping, 0x1ac3c)
DEF_NAMESPACE_REG(Iridix_PerceptControl, pong, 0x32bfc)

class Iridix_StrengthOutroi :
      public hwreg::RegisterBase<Iridix_StrengthOutroi, uint32_t> {
public:
    // Manual Strength value for outside of ROI
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(Iridix_StrengthOutroi, ping, 0x1ac40)
DEF_NAMESPACE_REG(Iridix_StrengthOutroi, pong, 0x32c00)

class Iridix_HorizontalRoi : public hwreg::RegisterBase<Iridix_HorizontalRoi, uint32_t> {
public:
    // Horizontal starting point of ROI
    DEF_FIELD(15, 0, roi_hor_start);
    // Horizontal ending point of ROI
    DEF_FIELD(31, 16, roi_hor_end);
};

DEF_NAMESPACE_REG(Iridix_HorizontalRoi, ping, 0x1ac44)
DEF_NAMESPACE_REG(Iridix_HorizontalRoi, pong, 0x32c04)

class Iridix_VerticalRoi : public hwreg::RegisterBase<Iridix_VerticalRoi, uint32_t> {
public:
    // Vertical starting point of ROI
    DEF_FIELD(15, 0, roi_ver_start);
    // Vertical ending point of ROI
    DEF_FIELD(31, 16, roi_ver_end);
};

DEF_NAMESPACE_REG(Iridix_VerticalRoi, ping, 0x1ac48)
DEF_NAMESPACE_REG(Iridix_VerticalRoi, pong, 0x32c08)

class Iridix_Config1 : public hwreg::RegisterBase<Iridix_Config1, uint32_t> {
public:
    // Iridix8 transform sensitivity to different areas of image
    DEF_FIELD(11, 8, svariance);
    // Manual Bright_Preserve value to control Iridix core
    DEF_FIELD(23, 16, bright_pr);
    // Iridix8 contrast control parameter
    DEF_FIELD(31, 24, contrast);
    // Selects between Iridix8 and Iridix7, 1=Iridix8 and 0=Iridix7
    DEF_BIT(0, filter_mux);
};

DEF_NAMESPACE_REG(Iridix_Config1, ping, 0x1ac4c)
DEF_NAMESPACE_REG(Iridix_Config1, pong, 0x32c0c)

class Iridix_DarkEnh : public hwreg::RegisterBase<Iridix_DarkEnh, uint32_t> {
public:
    // Manual Dark_Enhance value to control Iridix core
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Iridix_DarkEnh, ping, 0x1ac50)
DEF_NAMESPACE_REG(Iridix_DarkEnh, pong, 0x32c10)

class Iridix_FwdAlpha : public hwreg::RegisterBase<Iridix_FwdAlpha, uint32_t> {
public:
    // alpha for gamma_dl
    DEF_FIELD(17, 0, value);
};

DEF_NAMESPACE_REG(Iridix_FwdAlpha, ping, 0x1ac54)
DEF_NAMESPACE_REG(Iridix_FwdAlpha, pong, 0x32c14)

class Iridix_RevAlpha : public hwreg::RegisterBase<Iridix_RevAlpha, uint32_t> {
public:
    // alpha for gamma_dl
    DEF_FIELD(17, 0, value);
};

DEF_NAMESPACE_REG(Iridix_RevAlpha, ping, 0x1ac58)
DEF_NAMESPACE_REG(Iridix_RevAlpha, pong, 0x32c18)

class Iridix_ContextNo : public hwreg::RegisterBase<Iridix_ContextNo, uint32_t> {
public:
    // Context id of a input Frame
    DEF_FIELD(1, 0, value);
};

DEF_NAMESPACE_REG(Iridix_ContextNo, ping, 0x1ac5c)
DEF_NAMESPACE_REG(Iridix_ContextNo, pong, 0x32c1c)

class Iridix_WbOffset : public hwreg::RegisterBase<Iridix_WbOffset, uint32_t> {
public:
    // White balance offset
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(Iridix_WbOffset, ping, 0x1ac60)
DEF_NAMESPACE_REG(Iridix_WbOffset, pong, 0x32c20)

class Iridix_Gain1 : public hwreg::RegisterBase<Iridix_Gain1, uint32_t> {
public:
    // White balance gain for R
    DEF_FIELD(11, 0, gain_r);
    // White balance gain for GR
    DEF_FIELD(27, 16, gain_gr);
};

DEF_NAMESPACE_REG(Iridix_Gain1, ping, 0x1ac64)
DEF_NAMESPACE_REG(Iridix_Gain1, pong, 0x32c24)

class Iridix_Gain2 : public hwreg::RegisterBase<Iridix_Gain2, uint32_t> {
public:
    // White balance gain for GB
    DEF_FIELD(11, 0, gain_gb);
    // White balance gain for B
    DEF_FIELD(27, 16, gain_b);
};

DEF_NAMESPACE_REG(Iridix_Gain2, ping, 0x1ac68)
DEF_NAMESPACE_REG(Iridix_Gain2, pong, 0x32c28)

class Iridix_GtmSelect : public hwreg::RegisterBase<Iridix_GtmSelect, uint32_t> {
public:
    // Global Tone map select : 0 : Local TM 1: Full Global TM
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(Iridix_GtmSelect, ping, 0x1ac6c)
DEF_NAMESPACE_REG(Iridix_GtmSelect, pong, 0x32c2c)

class DemosaicRgb_Slope : public hwreg::RegisterBase<DemosaicRgb_Slope, uint32_t> {
public:
    //  Slope of vertical/horizontal blending threshold in 4.4
    //   logarithmic format.
    //   High values will tend to favor one direction over the other
    //    (depending on VH Thresh) while lower values will give smoother
    //    blending.
    DEF_FIELD(7, 0, vh_slope);
    // Slope of angular (45/135) blending threshold in 4.4 format.
    //   High values will tend to favor one direction over the other
    //    (depending on AA Thresh) while lower values will give smoother
    //    blending.
    DEF_FIELD(15, 8, aa_slope);
    // Slope of VH-AA blending threshold in 4.4 log format.
    //   High values will tend to favor one direction over the other
    //    (depending on VA Thresh)
    //  while lower values will give smoother blending.
    DEF_FIELD(23, 16, va_slope);
    // Slope of undefined blending threshold in 4.4 logarithmic format
    DEF_FIELD(31, 24, uu_slope);
};

DEF_NAMESPACE_REG(DemosaicRgb_Slope, ping, 0x1ae7c)
DEF_NAMESPACE_REG(DemosaicRgb_Slope, pong, 0x32e3c)

class DemosaicRgb_SatSlope : public hwreg::RegisterBase<DemosaicRgb_SatSlope, uint32_t> {
public:
    // Slope of saturation blending threshold in linear format 2.6
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_SatSlope, ping, 0x1ae80)
DEF_NAMESPACE_REG(DemosaicRgb_SatSlope, pong, 0x32e40)

class DemosaicRgb_Threshold0 :
      public hwreg::RegisterBase<DemosaicRgb_Threshold0, uint32_t> {
public:
    // Threshold for the range of vertical/horizontal blending
    //      The threshold defines the difference of vertical and
    //       horizontal gradients at which the vertical gradient will
    //       start to be taken into account in the blending (if VH Offset
    //        is set to 0).
    //       Setting the offset not null (or the slope low) will include
    //        proportion of the vertical
    //       gradient in the blending before even the gradient difference
    //        reaches the threshold (see VH Offset for more details).
    DEF_FIELD(11, 0, vh_thresh);
    // Threshold for the range of angular (45/135) blending.
    //   The threshold defines the difference of 45 and 135 gradients at
    //    which the 45 gradient will start to be taken into account in
    //    the
    //  blending (if AA Offset is set to 0).
    //   Setting the offset not null (or the slope low) will include
    //    proportion of the 45 gradient in the blending before
    //   even the gradient difierence reaches the threshold (see AA
    //    Offset for more details).
    DEF_FIELD(27, 16, aa_thresh);
};

DEF_NAMESPACE_REG(DemosaicRgb_Threshold0, ping, 0x1ae84)
DEF_NAMESPACE_REG(DemosaicRgb_Threshold0, pong, 0x32e44)

class DemosaicRgb_Threshold1 :
      public hwreg::RegisterBase<DemosaicRgb_Threshold1, uint32_t> {
public:
    // Threshold for the range of VH-AA blending.
    //   The threshold defines the difference of VH and AA gradients at
    //    which the VH gradient will start to be taken into account in
    //    the blending
    //   (if VA Offset is set to 0). Setting the offset not null (or the
    //     slope low) will include proportion of the VH gradient
    //   in the blending before even the gradient difference reaches the
    //    threshold (see VA Offiset for more details).
    DEF_FIELD(11, 0, va_thresh);
    // Threshold for the range of undefined blending
    DEF_FIELD(27, 16, uu_thresh);
};

DEF_NAMESPACE_REG(DemosaicRgb_Threshold1, ping, 0x1ae88)
DEF_NAMESPACE_REG(DemosaicRgb_Threshold1, pong, 0x32e48)

class DemosaicRgb_Threshold2 :
      public hwreg::RegisterBase<DemosaicRgb_Threshold2, uint32_t> {
public:
    // Threshold for the range of saturation blending  in signed 2.9 format
    DEF_FIELD(11, 0, sat_thresh);
    // Luminance threshold for directional sharpening
    DEF_FIELD(27, 16, lum_thresh);
};

DEF_NAMESPACE_REG(DemosaicRgb_Threshold2, ping, 0x1ae8c)
DEF_NAMESPACE_REG(DemosaicRgb_Threshold2, pong, 0x32e4c)

class DemosaicRgb_Offset0 : public hwreg::RegisterBase<DemosaicRgb_Offset0, uint32_t> {
public:
    // Offset for vertical/horizontal blending threshold
    DEF_FIELD(11, 0, vh_offset);
    // Offset for angular (A45/A135) blending threshold.
    //  This register has great impact on how AA Thresh is used.
    //   Setting this register to a value offset tells the blending
    //    process to weight the 45 and 135 gradients,
    //  at the threshold, with respectively offset/16 and 255 - (offset/16).
    //   If AA Thresh not equals to 0, these same blending weights apply
    //    from -AA Thresh to +AA Thresh.
    DEF_FIELD(27, 16, aa_offset);
};

DEF_NAMESPACE_REG(DemosaicRgb_Offset0, ping, 0x1ae90)
DEF_NAMESPACE_REG(DemosaicRgb_Offset0, pong, 0x32e50)

class DemosaicRgb_Offset1 : public hwreg::RegisterBase<DemosaicRgb_Offset1, uint32_t> {
public:
    //  Offset for VH-AA blending threshold. This register has great
    //   impact on how VA Thresh is used.
    //   Setting this register to a value offset tells the blending
    //    process to weight the VH and AA gradients,
    //  at the threshold, with respectively offset/16 and 255 - (offset/16).
    //  If VA Thresh not equals to 0, these same blending weights apply
    //   from -VA Thresh to +VA Thresh.
    DEF_FIELD(11, 0, va_offset);
    // Offset for undefined blending threshold
    DEF_FIELD(27, 16, uu_offset);
};

DEF_NAMESPACE_REG(DemosaicRgb_Offset1, ping, 0x1ae94)
DEF_NAMESPACE_REG(DemosaicRgb_Offset1, pong, 0x32e54)

class DemosaicRgb_Offset2 : public hwreg::RegisterBase<DemosaicRgb_Offset2, uint32_t> {
public:
    // Offset for saturation blending threshold in signed 2.9 format
    DEF_FIELD(11, 0, sat_offset);
    // Offset for AC blending threshold in signed 2.9 format
    DEF_FIELD(27, 16, ac_offset);
};

DEF_NAMESPACE_REG(DemosaicRgb_Offset2, ping, 0x1ae98)
DEF_NAMESPACE_REG(DemosaicRgb_Offset2, pong, 0x32e58)

class DemosaicRgb_SharpenAlternate :
      public hwreg::RegisterBase<DemosaicRgb_SharpenAlternate, uint32_t> {
public:
    // Directional sharp mask strength in signed 4.4 format
    DEF_FIELD(7, 0, sharp_alt_d);
    // Non-directional sharp mask strength in signed 4.4 format
    DEF_FIELD(15, 8, sharp_alt_ud);
    // Noise profile offset in logarithmic 4.4 format
    DEF_FIELD(23, 16, np_offset);
};

DEF_NAMESPACE_REG(DemosaicRgb_SharpenAlternate, ping, 0x1ae9c)
DEF_NAMESPACE_REG(DemosaicRgb_SharpenAlternate, pong, 0x32e5c)

class DemosaicRgb_DmscConfig :
      public hwreg::RegisterBase<DemosaicRgb_DmscConfig, uint32_t> {
public:
    // Debug output select. Set to 0x00 for normal operation.
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_DmscConfig, ping, 0x1aea0)
DEF_NAMESPACE_REG(DemosaicRgb_DmscConfig, pong, 0x32e60)

class DemosaicRgb_AlphaChannel :
      public hwreg::RegisterBase<DemosaicRgb_AlphaChannel, uint32_t> {
public:
    // Threshold for the range of AC blending in signed 2.9 format
    DEF_FIELD(11, 0, ac_thresh);
    // Slope of AC blending threshold in linear format 2.6
    DEF_FIELD(23, 16, ac_slope);
};

DEF_NAMESPACE_REG(DemosaicRgb_AlphaChannel, ping, 0x1aea4)
DEF_NAMESPACE_REG(DemosaicRgb_AlphaChannel, pong, 0x32e64)

class DemosaicRgb_FalseColor :
      public hwreg::RegisterBase<DemosaicRgb_FalseColor, uint32_t> {
public:
    // Slope (strength) of false color correction
    DEF_FIELD(7, 0, fc_slope);
    //  Slope (strength) of false colour correction after blending with
    //   saturation value in 2.6 unsigned format
    DEF_FIELD(15, 8, fc_alias_slope);
    //  Threshold of false colour correction after blending with
    //   saturation valuet in in 0.8 unsigned format
    DEF_FIELD(23, 16, fc_alias_thresh);
};

DEF_NAMESPACE_REG(DemosaicRgb_FalseColor, ping, 0x1aea8)
DEF_NAMESPACE_REG(DemosaicRgb_FalseColor, pong, 0x32e68)

class DemosaicRgb_NpOff : public hwreg::RegisterBase<DemosaicRgb_NpOff, uint32_t> {
public:
    // Noise profile black level offset
    DEF_FIELD(6, 0, np_off);
    // Defines how values below black level are obtained.
    //   0: Repeat the first table entry.
    //   1: Reflect the noise profile curve below black level.
    DEF_BIT(7, np_off_reflect);
};

DEF_NAMESPACE_REG(DemosaicRgb_NpOff, ping, 0x1aeac)
DEF_NAMESPACE_REG(DemosaicRgb_NpOff, pong, 0x32e6c)

class DemosaicRgb_Config11 : public hwreg::RegisterBase<DemosaicRgb_Config11, uint32_t> {
public:
    // Sharpen strength for L_Ld in unsigned 4.4 format
    DEF_FIELD(7, 0, sharp_alt_ld);
    // Sharpen strength for L_Ldu in unsigned 4.4 format
    DEF_FIELD(15, 8, sharp_alt_ldu);
    // Sharpen strength for L_Lu in unsigned 4.4 format
    DEF_FIELD(23, 16, sharp_alt_lu);
    // Sad amplifier in unsigned 4.4 format
    DEF_FIELD(31, 24, sad_amp);
};

DEF_NAMESPACE_REG(DemosaicRgb_Config11, ping, 0x1aeb0)
DEF_NAMESPACE_REG(DemosaicRgb_Config11, pong, 0x32e70)

class DemosaicRgb_MinDStrength :
      public hwreg::RegisterBase<DemosaicRgb_MinDStrength, uint32_t> {
public:
    //  Min threshold for the directional L_L in signed 2's complement
    //   s.12 format
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_MinDStrength, ping, 0x1aeb4)
DEF_NAMESPACE_REG(DemosaicRgb_MinDStrength, pong, 0x32e74)

class DemosaicRgb_MinUdStrength :
      public hwreg::RegisterBase<DemosaicRgb_MinUdStrength, uint32_t> {
public:
    //  Min threshold for the un-directional L_Lu in signed 2's
    //   complement s.12 format
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_MinUdStrength, ping, 0x1aeb8)
DEF_NAMESPACE_REG(DemosaicRgb_MinUdStrength, pong, 0x32e78)

class DemosaicRgb_SharpenAlgSelect :
      public hwreg::RegisterBase<DemosaicRgb_SharpenAlgSelect, uint32_t> {
public:
    // To select new sharp algorithm or not
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_SharpenAlgSelect, ping, 0x1aebc)
DEF_NAMESPACE_REG(DemosaicRgb_SharpenAlgSelect, pong, 0x32e7c)

class DemosaicRgb_Config12 : public hwreg::RegisterBase<DemosaicRgb_Config12, uint32_t> {
public:
    // Slope of undefined blending threshold in 4.4 logarithmic format
    DEF_FIELD(7, 0, uu_sh_slope);
    //  Level to which the green channel is considered low in which case
    //   the gradient is calculated using only the blue and red channels
    DEF_FIELD(15, 8, lg_det_thresh);
    //  Threshold applied to the inter-channel difference for detecting
    //   grey region
    DEF_FIELD(23, 16, grey_det_thresh);
};

DEF_NAMESPACE_REG(DemosaicRgb_Config12, ping, 0x1aec0)
DEF_NAMESPACE_REG(DemosaicRgb_Config12, pong, 0x32e80)

class DemosaicRgb_UuSh : public hwreg::RegisterBase<DemosaicRgb_UuSh, uint32_t> {
public:
    // Threshold for the range of undefined blending
    DEF_FIELD(11, 0, uu_sh_thresh);
    // Offset for undefined blending threshold
    DEF_FIELD(27, 16, uu_sh_offset);
};

DEF_NAMESPACE_REG(DemosaicRgb_UuSh, ping, 0x1aec4)
DEF_NAMESPACE_REG(DemosaicRgb_UuSh, pong, 0x32e84)

class DemosaicRgb_DetSlope : public hwreg::RegisterBase<DemosaicRgb_DetSlope, uint32_t> {
public:
    // Control the ramp of the linear thresholding for the low green detector
    DEF_FIELD(15, 0, lg_det_slope);
    // Control the ramp of the linear thresholding for the grey detector
    DEF_FIELD(31, 16, grey_det_slope);
};

DEF_NAMESPACE_REG(DemosaicRgb_DetSlope, ping, 0x1aec8)
DEF_NAMESPACE_REG(DemosaicRgb_DetSlope, pong, 0x32e88)

class DemosaicRgb_MaxD : public hwreg::RegisterBase<DemosaicRgb_MaxD, uint32_t> {
public:
    //  Max threshold for the directional L_L in signed 2's complement
    //   s1+0.12 format
    DEF_FIELD(12, 0, max_d_strength);
    //  Max threshold for the undirectional L_Lu in signed 2's complement
    //   s1+0.12 format
    DEF_FIELD(28, 16, max_ud_strength);
};

DEF_NAMESPACE_REG(DemosaicRgb_MaxD, ping, 0x1aecc)
DEF_NAMESPACE_REG(DemosaicRgb_MaxD, pong, 0x32e8c)

class DemosaicRgb_LumaLowD : public hwreg::RegisterBase<DemosaicRgb_LumaLowD, uint32_t> {
public:
    // Intensity values above this value will be sharpen
    DEF_FIELD(11, 0, luma_thresh_low_d);
    // Linear threshold offset corresponding to luma_thresh_low_d
    DEF_FIELD(23, 16, luma_offset_low_d);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaLowD, ping, 0x1aed0)
DEF_NAMESPACE_REG(DemosaicRgb_LumaLowD, pong, 0x32e90)

class DemosaicRgb_LumaSlopeLowD :
      public hwreg::RegisterBase<DemosaicRgb_LumaSlopeLowD, uint32_t> {
public:
    // Linear threshold slope corresponding to luma_thresh_low_d
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeLowD, ping, 0x1aed4)
DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeLowD, pong, 0x32e94)

class DemosaicRgb_LumaThreshHighD :
      public hwreg::RegisterBase<DemosaicRgb_LumaThreshHighD, uint32_t> {
public:
    // Intensity values below this value will be sharpen
    DEF_FIELD(27, 16, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaThreshHighD, ping, 0x1aed8)
DEF_NAMESPACE_REG(DemosaicRgb_LumaThreshHighD, pong, 0x32e98)

class DemosaicRgb_LumaSlopeHighD :
      public hwreg::RegisterBase<DemosaicRgb_LumaSlopeHighD, uint32_t> {
public:
    // Linear threshold slope corresponding to luma_thresh_high_d
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeHighD, ping, 0x1aedc)
DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeHighD, pong, 0x32e9c)

class DemosaicRgb_LumaLowUd :
      public hwreg::RegisterBase<DemosaicRgb_LumaLowUd, uint32_t> {
public:
    // Intensity values above this value will be sharpen
    DEF_FIELD(11, 0, luma_thresh_low_ud);
    // Linear threshold offset corresponding to luma_thresh_low_ud
    DEF_FIELD(23, 16, luma_offset_low_ud);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaLowUd, ping, 0x1aee0)
DEF_NAMESPACE_REG(DemosaicRgb_LumaLowUd, pong, 0x32ea0)

class DemosaicRgb_LumaSlopeLowUd :
      public hwreg::RegisterBase<DemosaicRgb_LumaSlopeLowUd, uint32_t> {
public:
    // Linear threshold slope corresponding to luma_thresh_low_ud
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeLowUd, ping, 0x1aee4)
DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeLowUd, pong, 0x32ea4)

class DemosaicRgb_LumaThreshHighUd :
      public hwreg::RegisterBase<DemosaicRgb_LumaThreshHighUd, uint32_t> {
public:
    // Intensity values below this value will be sharpen
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaThreshHighUd, ping, 0x1aee8)
DEF_NAMESPACE_REG(DemosaicRgb_LumaThreshHighUd, pong, 0x32ea8)

class DemosaicRgb_LumaSlopeHighUd :
      public hwreg::RegisterBase<DemosaicRgb_LumaSlopeHighUd, uint32_t> {
public:
    // Linear threshold slope corresponding to luma_thresh_high_ud
    DEF_FIELD(19, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeHighUd, ping, 0x1aeec)
DEF_NAMESPACE_REG(DemosaicRgb_LumaSlopeHighUd, pong, 0x32eac)

class DemosaicRgbir_RgbirConfig :
      public hwreg::RegisterBase<DemosaicRgbir_RgbirConfig, uint32_t> {
public:
    //  Debug related configurations to select out different internal
    //   signals, and normal RGBIR will be outputted by default
    DEF_FIELD(2, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_RgbirConfig, ping, 0x1af70)
DEF_NAMESPACE_REG(DemosaicRgbir_RgbirConfig, pong, 0x32f30)

class DemosaicRgbir_ClipLevel :
      public hwreg::RegisterBase<DemosaicRgbir_ClipLevel, uint32_t> {
public:
    // clip level
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_ClipLevel, ping, 0x1af74)
DEF_NAMESPACE_REG(DemosaicRgbir_ClipLevel, pong, 0x32f34)

class DemosaicRgbir_ClipDebloom :
      public hwreg::RegisterBase<DemosaicRgbir_ClipDebloom, uint32_t> {
public:
    // clip level for debloom
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_ClipDebloom, ping, 0x1af78)
DEF_NAMESPACE_REG(DemosaicRgbir_ClipDebloom, pong, 0x32f38)

class DemosaicRgbir_IrOnBlueRow :
      public hwreg::RegisterBase<DemosaicRgbir_IrOnBlueRow, uint32_t> {
public:
    // to indicate that the IR is on the same line of Blue
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrOnBlueRow, ping, 0x1af7c)
DEF_NAMESPACE_REG(DemosaicRgbir_IrOnBlueRow, pong, 0x32f3c)

class DemosaicRgbir_DeclipMode :
      public hwreg::RegisterBase<DemosaicRgbir_DeclipMode, uint32_t> {
public:
    // Declip mode
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_DeclipMode, ping, 0x1af80)
DEF_NAMESPACE_REG(DemosaicRgbir_DeclipMode, pong, 0x32f40)

class DemosaicRgbir_Gain : public hwreg::RegisterBase<DemosaicRgbir_Gain, uint32_t> {
public:
    // gain for red
    DEF_FIELD(11, 0, gain_r);
    // gain for blue
    DEF_FIELD(27, 16, gain_b);
};

DEF_NAMESPACE_REG(DemosaicRgbir_Gain, ping, 0x1af84)
DEF_NAMESPACE_REG(DemosaicRgbir_Gain, pong, 0x32f44)

class DemosaicRgbir_StaticGain :
      public hwreg::RegisterBase<DemosaicRgbir_StaticGain, uint32_t> {
public:
    // static gain for red
    DEF_FIELD(11, 0, static_gain_r);
    // static gain for red
    DEF_FIELD(27, 16, static_gain_b);
};

DEF_NAMESPACE_REG(DemosaicRgbir_StaticGain, ping, 0x1af88)
DEF_NAMESPACE_REG(DemosaicRgbir_StaticGain, pong, 0x32f48)

class DemosaicRgbir_StaticGainI :
      public hwreg::RegisterBase<DemosaicRgbir_StaticGainI, uint32_t> {
public:
    // static gain for ir
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_StaticGainI, ping, 0x1af8c)
DEF_NAMESPACE_REG(DemosaicRgbir_StaticGainI, pong, 0x32f4c)

class DemosaicRgbir_InterpolationDirectionality :
      public hwreg::RegisterBase<DemosaicRgbir_InterpolationDirectionality, uint32_t> {
public:
    // Interpolation Directionality
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_InterpolationDirectionality, ping, 0x1af90)
DEF_NAMESPACE_REG(DemosaicRgbir_InterpolationDirectionality, pong, 0x32f50)

class DemosaicRgbir_SharpLimit :
      public hwreg::RegisterBase<DemosaicRgbir_SharpLimit, uint32_t> {
public:
    // sharp limit
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_SharpLimit, ping, 0x1af94)
DEF_NAMESPACE_REG(DemosaicRgbir_SharpLimit, pong, 0x32f54)

class DemosaicRgbir_SharpHigh :
      public hwreg::RegisterBase<DemosaicRgbir_SharpHigh, uint32_t> {
public:
    // sharp high
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_SharpHigh, ping, 0x1af98)
DEF_NAMESPACE_REG(DemosaicRgbir_SharpHigh, pong, 0x32f58)

class DemosaicRgbir_SharpLow :
      public hwreg::RegisterBase<DemosaicRgbir_SharpLow, uint32_t> {
public:
    // sharp low
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_SharpLow, ping, 0x1af9c)
DEF_NAMESPACE_REG(DemosaicRgbir_SharpLow, pong, 0x32f5c)

class DemosaicRgbir_FcLow : public hwreg::RegisterBase<DemosaicRgbir_FcLow, uint32_t> {
public:
    // fc low
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_FcLow, ping, 0x1afa0)
DEF_NAMESPACE_REG(DemosaicRgbir_FcLow, pong, 0x32f60)

class DemosaicRgbir_FcGrad : public hwreg::RegisterBase<DemosaicRgbir_FcGrad, uint32_t> {
public:
    // fc grad
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(DemosaicRgbir_FcGrad, ping, 0x1afa4)
DEF_NAMESPACE_REG(DemosaicRgbir_FcGrad, pong, 0x32f64)

class DemosaicRgbir_IrCorrectMat0001 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat0001, uint32_t> {
public:
    // ir correct mat 00
    DEF_FIELD(11, 0, ir_correct_mat00);
    // ir correct mat 01
    DEF_FIELD(27, 16, ir_correct_mat01);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat0001, ping, 0x1afa8)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat0001, pong, 0x32f68)

class DemosaicRgbir_IrCorrectMat0203 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat0203, uint32_t> {
public:
    // ir correct mat 02
    DEF_FIELD(11, 0, ir_correct_mat02);
    // ir correct mat 03
    DEF_FIELD(27, 16, ir_correct_mat03);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat0203, ping, 0x1afac)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat0203, pong, 0x32f6c)

class DemosaicRgbir_IrCorrectMat1011 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat1011, uint32_t> {
public:
    // ir correct mat 10
    DEF_FIELD(11, 0, ir_correct_mat10);
    // ir correct mat 11
    DEF_FIELD(27, 16, ir_correct_mat11);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat1011, ping, 0x1afb0)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat1011, pong, 0x32f70)

class DemosaicRgbir_IrCorrectMat1213 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat1213, uint32_t> {
public:
    // ir correct mat 12
    DEF_FIELD(11, 0, ir_correct_mat12);
    // ir correct mat 13
    DEF_FIELD(27, 16, ir_correct_mat13);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat1213, ping, 0x1afb4)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat1213, pong, 0x32f74)

class DemosaicRgbir_IrCorrectMat2021 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat2021, uint32_t> {
public:
    // ir correct mat 20
    DEF_FIELD(11, 0, ir_correct_mat20);
    // ir correct mat 21
    DEF_FIELD(27, 16, ir_correct_mat21);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat2021, ping, 0x1afb8)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat2021, pong, 0x32f78)

class DemosaicRgbir_IrCorrectMat2223 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat2223, uint32_t> {
public:
    // ir correct mat 22
    DEF_FIELD(11, 0, ir_correct_mat22);
    // ir correct mat 23
    DEF_FIELD(27, 16, ir_correct_mat23);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat2223, ping, 0x1afbc)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat2223, pong, 0x32f7c)

class DemosaicRgbir_IrCorrectMat3031 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat3031, uint32_t> {
public:
    // ir correct mat 30
    DEF_FIELD(11, 0, ir_correct_mat30);
    // ir correct mat 31
    DEF_FIELD(27, 16, ir_correct_mat31);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat3031, ping, 0x1afc0)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat3031, pong, 0x32f80)

class DemosaicRgbir_IrCorrectMat3233 :
      public hwreg::RegisterBase<DemosaicRgbir_IrCorrectMat3233, uint32_t> {
public:
    // ir correct mat 32
    DEF_FIELD(11, 0, ir_correct_mat32);
    // ir correct mat 33
    DEF_FIELD(27, 16, ir_correct_mat33);
};

DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat3233, ping, 0x1afc4)
DEF_NAMESPACE_REG(DemosaicRgbir_IrCorrectMat3233, pong, 0x32f84)

class PurpleFringeCorrection_UseColorCorrectedRgb :
      public hwreg::RegisterBase<PurpleFringeCorrection_UseColorCorrectedRgb, uint32_t> {
public:
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_UseColorCorrectedRgb, ping, 0x1afc8)
DEF_NAMESPACE_REG(PurpleFringeCorrection_UseColorCorrectedRgb, pong, 0x32f88)

class PurpleFringeCorrection_HueStrength :
      public hwreg::RegisterBase<PurpleFringeCorrection_HueStrength, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HueStrength, ping, 0x1afcc)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HueStrength, pong, 0x32f8c)

class PurpleFringeCorrection_Strength1 :
      public hwreg::RegisterBase<PurpleFringeCorrection_Strength1, uint32_t> {
public:
    DEF_FIELD(11, 0, sat_strength);
    DEF_FIELD(27, 16, luma_strength);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Strength1, ping, 0x1afd0)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Strength1, pong, 0x32f90)

class PurpleFringeCorrection_Strength2 :
      public hwreg::RegisterBase<PurpleFringeCorrection_Strength2, uint32_t> {
public:
    DEF_FIELD(11, 0, purple_strength);
    DEF_FIELD(23, 16, saturation_strength);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Strength2, ping, 0x1afd4)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Strength2, pong, 0x32f94)

class PurpleFringeCorrection_OffCenterMult :
      public hwreg::RegisterBase<PurpleFringeCorrection_OffCenterMult, uint32_t> {
public:
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_OffCenterMult, ping, 0x1afd8)
DEF_NAMESPACE_REG(PurpleFringeCorrection_OffCenterMult, pong, 0x32f98)

class PurpleFringeCorrection_Center :
      public hwreg::RegisterBase<PurpleFringeCorrection_Center, uint32_t> {
public:
    DEF_FIELD(15, 0, center_x);
    DEF_FIELD(31, 16, center_y);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Center, ping, 0x1afdc)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Center, pong, 0x32f9c)

class PurpleFringeCorrection_ColorConversionMatrixCoeffRr :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffRr, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRr, ping, 0x1afe0)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRr, pong, 0x32fa0)

class PurpleFringeCorrection_ColorConversionMatrixCoeffRg :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffRg, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRg, ping, 0x1afe4)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRg, pong, 0x32fa4)

class PurpleFringeCorrection_ColorConversionMatrixCoeffRb :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffRb, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRb, ping, 0x1afe8)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffRb, pong, 0x32fa8)

class PurpleFringeCorrection_ColorConversionMatrixCoeffGr :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffGr, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGr, ping, 0x1afec)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGr, pong, 0x32fac)

class PurpleFringeCorrection_ColorConversionMatrixCoeffGg :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffGg, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGg, ping, 0x1aff0)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGg, pong, 0x32fb0)

class PurpleFringeCorrection_ColorConversionMatrixCoeffGb :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffGb, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGb, ping, 0x1aff4)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffGb, pong, 0x32fb4)

class PurpleFringeCorrection_ColorConversionMatrixCoeffBr :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffBr, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBr, ping, 0x1aff8)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBr, pong, 0x32fb8)

class PurpleFringeCorrection_ColorConversionMatrixCoeffBg :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffBg, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBg, ping, 0x1affc)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBg, pong, 0x32fbc)

class PurpleFringeCorrection_ColorConversionMatrixCoeffBb :
      public hwreg::RegisterBase<PurpleFringeCorrection_ColorConversionMatrixCoeffBb, uint32_t> {
public:
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBb, ping, 0x1b000)
DEF_NAMESPACE_REG(PurpleFringeCorrection_ColorConversionMatrixCoeffBb, pong, 0x32fc0)

class PurpleFringeCorrection_Sad :
      public hwreg::RegisterBase<PurpleFringeCorrection_Sad, uint32_t> {
public:
    DEF_FIELD(11, 0, sad_slope);
    DEF_FIELD(27, 16, sad_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Sad, ping, 0x1b004)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Sad, pong, 0x32fc4)

class PurpleFringeCorrection_SadThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_SadThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_SadThresh, ping, 0x1b008)
DEF_NAMESPACE_REG(PurpleFringeCorrection_SadThresh, pong, 0x32fc8)

class PurpleFringeCorrection_HueLow :
      public hwreg::RegisterBase<PurpleFringeCorrection_HueLow, uint32_t> {
public:
    DEF_FIELD(11, 0, hue_low_slope);
    DEF_FIELD(27, 16, hue_low_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HueLow, ping, 0x1b00c)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HueLow, pong, 0x32fcc)

class PurpleFringeCorrection_HueLowThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_HueLowThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HueLowThresh, ping, 0x1b010)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HueLowThresh, pong, 0x32fd0)

class PurpleFringeCorrection_HueHigh :
      public hwreg::RegisterBase<PurpleFringeCorrection_HueHigh, uint32_t> {
public:
    DEF_FIELD(11, 0, hue_high_slope);
    DEF_FIELD(27, 16, hue_high_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HueHigh, ping, 0x1b014)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HueHigh, pong, 0x32fd4)

class PurpleFringeCorrection_HueHighThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_HueHighThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HueHighThresh, ping, 0x1b018)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HueHighThresh, pong, 0x32fd8)

class PurpleFringeCorrection_SatLow :
      public hwreg::RegisterBase<PurpleFringeCorrection_SatLow, uint32_t> {
public:
    DEF_FIELD(11, 0, sat_low_slope);
    DEF_FIELD(27, 16, sat_low_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_SatLow, ping, 0x1b01c)
DEF_NAMESPACE_REG(PurpleFringeCorrection_SatLow, pong, 0x32fdc)

class PurpleFringeCorrection_SatLowThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_SatLowThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_SatLowThresh, ping, 0x1b020)
DEF_NAMESPACE_REG(PurpleFringeCorrection_SatLowThresh, pong, 0x32fe0)

class PurpleFringeCorrection_SatHigh :
      public hwreg::RegisterBase<PurpleFringeCorrection_SatHigh, uint32_t> {
public:
    DEF_FIELD(11, 0, sat_high_slope);
    DEF_FIELD(27, 16, sat_high_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_SatHigh, ping, 0x1b024)
DEF_NAMESPACE_REG(PurpleFringeCorrection_SatHigh, pong, 0x32fe4)

class PurpleFringeCorrection_SatHighThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_SatHighThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_SatHighThresh, ping, 0x1b028)
DEF_NAMESPACE_REG(PurpleFringeCorrection_SatHighThresh, pong, 0x32fe8)

class PurpleFringeCorrection_Luma1Low :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma1Low, uint32_t> {
public:
    DEF_FIELD(11, 0, luma1_low_slope);
    DEF_FIELD(27, 16, luma1_low_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1Low, ping, 0x1b02c)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1Low, pong, 0x32fec)

class PurpleFringeCorrection_Luma1LowThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma1LowThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1LowThresh, ping, 0x1b030)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1LowThresh, pong, 0x32ff0)

class PurpleFringeCorrection_Luma1High :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma1High, uint32_t> {
public:
    DEF_FIELD(11, 0, luma1_high_slope);
    DEF_FIELD(27, 16, luma1_high_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1High, ping, 0x1b034)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1High, pong, 0x32ff4)

class PurpleFringeCorrection_Luma1HighThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma1HighThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1HighThresh, ping, 0x1b038)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma1HighThresh, pong, 0x32ff8)

class PurpleFringeCorrection_Luma2Low :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma2Low, uint32_t> {
public:
    DEF_FIELD(11, 0, luma2_low_slope);
    DEF_FIELD(27, 16, luma2_low_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2Low, ping, 0x1b03c)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2Low, pong, 0x32ffc)

class PurpleFringeCorrection_Luma2LowThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma2LowThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2LowThresh, ping, 0x1b040)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2LowThresh, pong, 0x33000)

class PurpleFringeCorrection_Luma2High :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma2High, uint32_t> {
public:
    DEF_FIELD(11, 0, luma2_high_slope);
    DEF_FIELD(27, 16, luma2_high_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2High, ping, 0x1b044)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2High, pong, 0x33004)

class PurpleFringeCorrection_Luma2HighThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_Luma2HighThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2HighThresh, ping, 0x1b048)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Luma2HighThresh, pong, 0x33008)

class PurpleFringeCorrection_Hsl :
      public hwreg::RegisterBase<PurpleFringeCorrection_Hsl, uint32_t> {
public:
    DEF_FIELD(11, 0, hsl_slope);
    DEF_FIELD(27, 16, hsl_offset);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_Hsl, ping, 0x1b04c)
DEF_NAMESPACE_REG(PurpleFringeCorrection_Hsl, pong, 0x3300c)

class PurpleFringeCorrection_HslThresh :
      public hwreg::RegisterBase<PurpleFringeCorrection_HslThresh, uint32_t> {
public:
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_HslThresh, ping, 0x1b050)
DEF_NAMESPACE_REG(PurpleFringeCorrection_HslThresh, pong, 0x33010)

class PurpleFringeCorrection_DebugSel :
      public hwreg::RegisterBase<PurpleFringeCorrection_DebugSel, uint32_t> {
public:
    // 0: normal operation
    // 1: radial weight in 0.8 format
    // 2: sad_mask in 0.12 format
    // 3: hue mask in 0.12 format
    // 4: saturation mask in 0.12 format
    // 5: luma mask in 12.0 format
    // 6: pf mask in 12.0 format
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(PurpleFringeCorrection_DebugSel, ping, 0x1b054)
DEF_NAMESPACE_REG(PurpleFringeCorrection_DebugSel, pong, 0x33014)

class ColorConversionMatrix_Enable :
      public hwreg::RegisterBase<ColorConversionMatrix_Enable, uint32_t> {
public:
    // Color matrix enable: 0=off 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_Enable, ping, 0x1b07c)
DEF_NAMESPACE_REG(ColorConversionMatrix_Enable, pong, 0x3303c)

class ColorConversionMatrix_CoefftRR :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftRR, uint32_t> {
public:
    // Matrix coefficient for red-red multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRR, ping, 0x1b080)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRR, pong, 0x33040)

class ColorConversionMatrix_CoefftRG :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftRG, uint32_t> {
public:
    // Matrix coefficient for red-green multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRG, ping, 0x1b084)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRG, pong, 0x33044)

class ColorConversionMatrix_CoefftRB :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftRB, uint32_t> {
public:
    // Matrix coefficient for red-blue multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRB, ping, 0x1b088)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRB, pong, 0x33048)

class ColorConversionMatrix_CoefftRIr :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftRIr, uint32_t> {
public:
    // Matrix coefficient for red-ir multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRIr, ping, 0x1b08c)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftRIr, pong, 0x3304c)

class ColorConversionMatrix_CoefftGR :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftGR, uint32_t> {
public:
    // Matrix coefficient for green-red multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGR, ping, 0x1b090)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGR, pong, 0x33050)

class ColorConversionMatrix_CoefftGG :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftGG, uint32_t> {
public:
    // Matrix coefficient for green-green multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGG, ping, 0x1b094)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGG, pong, 0x33054)

class ColorConversionMatrix_CoefftGB :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftGB, uint32_t> {
public:
    // Matrix coefficient for green-blue multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGB, ping, 0x1b098)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGB, pong, 0x33058)

class ColorConversionMatrix_CoefftGIr :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftGIr, uint32_t> {
public:
    // Matrix coefficient for green-ir multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGIr, ping, 0x1b09c)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftGIr, pong, 0x3305c)

class ColorConversionMatrix_CoefftBR :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftBR, uint32_t> {
public:
    // Matrix coefficient for blue-red multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBR, ping, 0x1b0a0)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBR, pong, 0x33060)

class ColorConversionMatrix_CoefftBG :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftBG, uint32_t> {
public:
    // Matrix coefficient for blue-green multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBG, ping, 0x1b0a4)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBG, pong, 0x33064)

class ColorConversionMatrix_CoefftBB :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftBB, uint32_t> {
public:
    // Matrix coefficient for blue-blue multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBB, ping, 0x1b0a8)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBB, pong, 0x33068)

class ColorConversionMatrix_CoefftBIr :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftBIr, uint32_t> {
public:
    // Matrix coefficient for blue-ir multiplier
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBIr, ping, 0x1b0ac)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftBIr, pong, 0x3306c)

class ColorConversionMatrix_CoefftWbR :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftWbR, uint32_t> {
public:
    // gain for Red channel for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbR, ping, 0x1b0b0)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbR, pong, 0x33070)

class ColorConversionMatrix_CoefftWbG :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftWbG, uint32_t> {
public:
    // gain for Green channel for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbG, ping, 0x1b0b4)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbG, pong, 0x33074)

class ColorConversionMatrix_CoefftWbB :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftWbB, uint32_t> {
public:
    // gain for Blue channel for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbB, ping, 0x1b0b8)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbB, pong, 0x33078)

class ColorConversionMatrix_CoefftWbIr :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftWbIr, uint32_t> {
public:
    // gain for IR channel for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbIr, ping, 0x1b0bc)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftWbIr, pong, 0x3307c)

class ColorConversionMatrix_CoefftFogOffsetR :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftFogOffsetR, uint32_t> {
public:
    // Offset R for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetR, ping, 0x1b0c0)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetR, pong, 0x33080)

class ColorConversionMatrix_CoefftFogOffsetG :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftFogOffsetG, uint32_t> {
public:
    // Offset G for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetG, ping, 0x1b0c4)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetG, pong, 0x33084)

class ColorConversionMatrix_CoefftFogOffsetB :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftFogOffsetB, uint32_t> {
public:
    // Offset B for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetB, ping, 0x1b0c8)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetB, pong, 0x33088)

class ColorConversionMatrix_CoefftFogOffsetIr :
      public hwreg::RegisterBase<ColorConversionMatrix_CoefftFogOffsetIr, uint32_t> {
public:
    // Offset Ir for antifog function
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetIr, ping, 0x1b0cc)
DEF_NAMESPACE_REG(ColorConversionMatrix_CoefftFogOffsetIr, pong, 0x3308c)

class ColorNoiseReduction_SquareRootEnable :
      public hwreg::RegisterBase<ColorNoiseReduction_SquareRootEnable, uint32_t> {
public:
    // pre-CNR square root and the post-CNR square modules enable condition
    //      enable: 0=off 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_SquareRootEnable, ping, 0x1b0d0)
DEF_NAMESPACE_REG(ColorNoiseReduction_SquareRootEnable, pong, 0x33090)

class ColorNoiseReduction_Enable :
      public hwreg::RegisterBase<ColorNoiseReduction_Enable, uint32_t> {
public:
    // CNR enable: 0=off 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Enable, ping, 0x1b0d4)
DEF_NAMESPACE_REG(ColorNoiseReduction_Enable, pong, 0x33094)

class ColorNoiseReduction_DebugReg :
      public hwreg::RegisterBase<ColorNoiseReduction_DebugReg, uint32_t> {
public:
    // CNR Debug: 0=off 1=on
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_DebugReg, ping, 0x1b0d8)
DEF_NAMESPACE_REG(ColorNoiseReduction_DebugReg, pong, 0x33098)

class ColorNoiseReduction_Mode :
      public hwreg::RegisterBase<ColorNoiseReduction_Mode, uint32_t> {
public:
    // CNR enable: 0=off 1=on
    DEF_FIELD(7, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Mode, ping, 0x1b0dc)
DEF_NAMESPACE_REG(ColorNoiseReduction_Mode, pong, 0x3309c)

class ColorNoiseReduction_DeltaFactor :
      public hwreg::RegisterBase<ColorNoiseReduction_DeltaFactor, uint32_t> {
public:
    // CNR enable: 0=off 1=on
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_DeltaFactor, ping, 0x1b0e0)
DEF_NAMESPACE_REG(ColorNoiseReduction_DeltaFactor, pong, 0x330a0)

class ColorNoiseReduction_EffectiveKernel :
      public hwreg::RegisterBase<ColorNoiseReduction_EffectiveKernel, uint32_t> {
public:
    // Effective kernel
    DEF_FIELD(6, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_EffectiveKernel, ping, 0x1b0e4)
DEF_NAMESPACE_REG(ColorNoiseReduction_EffectiveKernel, pong, 0x330a4)

class ColorNoiseReduction_UCenter :
      public hwreg::RegisterBase<ColorNoiseReduction_UCenter, uint32_t> {
public:
    // Coordinates of u center
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UCenter, ping, 0x1b0e8)
DEF_NAMESPACE_REG(ColorNoiseReduction_UCenter, pong, 0x330a8)

class ColorNoiseReduction_VCenter :
      public hwreg::RegisterBase<ColorNoiseReduction_VCenter, uint32_t> {
public:
    // Coordinates of v center
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_VCenter, ping, 0x1b0ec)
DEF_NAMESPACE_REG(ColorNoiseReduction_VCenter, pong, 0x330ac)

class ColorNoiseReduction_GlobalOffset :
      public hwreg::RegisterBase<ColorNoiseReduction_GlobalOffset, uint32_t> {
public:
    // umean1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_GlobalOffset, ping, 0x1b0f0)
DEF_NAMESPACE_REG(ColorNoiseReduction_GlobalOffset, pong, 0x330b0)

class ColorNoiseReduction_GlobalSlope :
      public hwreg::RegisterBase<ColorNoiseReduction_GlobalSlope, uint32_t> {
public:
    // umean1 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_GlobalSlope, ping, 0x1b0f4)
DEF_NAMESPACE_REG(ColorNoiseReduction_GlobalSlope, pong, 0x330b4)

class ColorNoiseReduction_UvSeg1Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_UvSeg1Threshold, uint32_t> {
public:
    // uv_seg1 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Threshold, ping, 0x1b0f8)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Threshold, pong, 0x330b8)

class ColorNoiseReduction_UvSeg1Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_UvSeg1Offset, uint32_t> {
public:
    // uv_seg1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Offset, ping, 0x1b0fc)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Offset, pong, 0x330bc)

class ColorNoiseReduction_UvSeg1Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_UvSeg1Slope, uint32_t> {
public:
    // uv_seg1 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Slope, ping, 0x1b100)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvSeg1Slope, pong, 0x330c0)

class ColorNoiseReduction_Umean1Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean1Threshold, uint32_t> {
public:
    // umean1 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Threshold, ping, 0x1b104)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Threshold, pong, 0x330c4)

class ColorNoiseReduction_Umean1Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean1Offset, uint32_t> {
public:
    // umean1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Offset, ping, 0x1b108)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Offset, pong, 0x330c8)

class ColorNoiseReduction_Umean1Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean1Slope, uint32_t> {
public:
    // umean1 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Slope, ping, 0x1b10c)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean1Slope, pong, 0x330cc)

class ColorNoiseReduction_Umean2Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean2Threshold, uint32_t> {
public:
    // umean2 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Threshold, ping, 0x1b110)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Threshold, pong, 0x330d0)

class ColorNoiseReduction_Umean2Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean2Offset, uint32_t> {
public:
    // umean2 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Offset, ping, 0x1b114)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Offset, pong, 0x330d4)

class ColorNoiseReduction_Umean2Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_Umean2Slope, uint32_t> {
public:
    // umean2 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Slope, ping, 0x1b118)
DEF_NAMESPACE_REG(ColorNoiseReduction_Umean2Slope, pong, 0x330d8)

class ColorNoiseReduction_Vmean1Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean1Threshold, uint32_t> {
public:
    // vmean1 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Threshold, ping, 0x1b11c)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Threshold, pong, 0x330dc)

class ColorNoiseReduction_Vmean1Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean1Offset, uint32_t> {
public:
    // vmean1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Offset, ping, 0x1b120)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Offset, pong, 0x330e0)

class ColorNoiseReduction_Vmean1Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean1Slope, uint32_t> {
public:
    // vmean1 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Slope, ping, 0x1b124)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean1Slope, pong, 0x330e4)

class ColorNoiseReduction_Vmean2Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean2Threshold, uint32_t> {
public:
    // vmean2 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Threshold, ping, 0x1b128)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Threshold, pong, 0x330e8)

class ColorNoiseReduction_Vmean2Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean2Offset, uint32_t> {
public:
    // vmean2 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Offset, ping, 0x1b12c)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Offset, pong, 0x330ec)

class ColorNoiseReduction_Vmean2Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_Vmean2Slope, uint32_t> {
public:
    // vmean2 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Slope, ping, 0x1b130)
DEF_NAMESPACE_REG(ColorNoiseReduction_Vmean2Slope, pong, 0x330f0)

class ColorNoiseReduction_UvVar1Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar1Threshold, uint32_t> {
public:
    // uv_var1 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Threshold, ping, 0x1b134)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Threshold, pong, 0x330f4)

class ColorNoiseReduction_UvVar1Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar1Offset, uint32_t> {
public:
    // uv_var1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Offset, ping, 0x1b138)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Offset, pong, 0x330f8)

class ColorNoiseReduction_UvVar1Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar1Slope, uint32_t> {
public:
    // uv_var2 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Slope, ping, 0x1b13c)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar1Slope, pong, 0x330fc)

class ColorNoiseReduction_UvVar2Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar2Threshold, uint32_t> {
public:
    // uv_var2 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Threshold, ping, 0x1b140)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Threshold, pong, 0x33100)

class ColorNoiseReduction_UvVar2Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar2Offset, uint32_t> {
public:
    // uv_var2 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Offset, ping, 0x1b144)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Offset, pong, 0x33104)

class ColorNoiseReduction_UvVar2Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_UvVar2Slope, uint32_t> {
public:
    // uv_var2 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Slope, ping, 0x1b148)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvVar2Slope, pong, 0x33108)

class ColorNoiseReduction_Scale :
      public hwreg::RegisterBase<ColorNoiseReduction_Scale, uint32_t> {
public:
    // uv_var1 scale
    DEF_FIELD(5, 0, uv_var1_scale);
    // uv_var2 scale
    DEF_FIELD(13, 8, uv_var2_scale);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Scale, ping, 0x1b14c)
DEF_NAMESPACE_REG(ColorNoiseReduction_Scale, pong, 0x3310c)

class ColorNoiseReduction_UvDelta1Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta1Threshold, uint32_t> {
public:
    // uv_delta1 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Threshold, ping, 0x1b150)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Threshold, pong, 0x33110)

class ColorNoiseReduction_UvDelta1Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta1Offset, uint32_t> {
public:
    // uv_delta1 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Offset, ping, 0x1b154)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Offset, pong, 0x33114)

class ColorNoiseReduction_UvDelta1Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta1Slope, uint32_t> {
public:
    // uv_delta1 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Slope, ping, 0x1b158)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta1Slope, pong, 0x33118)

class ColorNoiseReduction_UvDelta2Threshold :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta2Threshold, uint32_t> {
public:
    // uv_delta2 threshold
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Threshold, ping, 0x1b15c)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Threshold, pong, 0x3311c)

class ColorNoiseReduction_UvDelta2Offset :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta2Offset, uint32_t> {
public:
    // uv_delta2 offset
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Offset, ping, 0x1b160)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Offset, pong, 0x33120)

class ColorNoiseReduction_UvDelta2Slope :
      public hwreg::RegisterBase<ColorNoiseReduction_UvDelta2Slope, uint32_t> {
public:
    // uv_delta2 slope
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Slope, ping, 0x1b164)
DEF_NAMESPACE_REG(ColorNoiseReduction_UvDelta2Slope, pong, 0x33124)

class ColorNoiseReduction_Status :
      public hwreg::RegisterBase<ColorNoiseReduction_Status, uint32_t> {
public:
    // CNR Debug Port
    DEF_FIELD(15, 0, statusa);
    // CNR Debug Port
    DEF_FIELD(31, 16, statusb);
};

DEF_NAMESPACE_REG(ColorNoiseReduction_Status, ping, 0x1b168)
DEF_NAMESPACE_REG(ColorNoiseReduction_Status, pong, 0x33128)

class NonequidistantGamma_SrgbLutEnable :
      public hwreg::RegisterBase<NonequidistantGamma_SrgbLutEnable, uint32_t> {
public:
    // enables gamma sRGB
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(NonequidistantGamma_SrgbLutEnable, ping, 0x1b16c)
DEF_NAMESPACE_REG(NonequidistantGamma_SrgbLutEnable, pong, 0x3312c)

class Lumvar_ActiveDim : public hwreg::RegisterBase<Lumvar_ActiveDim, uint32_t> {
public:
    //  Active width. This depends on the position of the luma variance
    //   module. if this module is connected to the
    //       full resolution pipeline, then the active_width should be
    //        the full resolution frame width.
    //      if its in the downscaled pipeline, then the active_width
    //       should be the post-scaler width
    DEF_FIELD(15, 0, active_width);
    //  Active height. This depends on the position of the luma variance
    //   module. if this module is connected to the
    //       full resolution pipeline, then the active_height should be
    //        the full resolution frame height.
    //      if its in the downscaled pipeline, then the active_height
    //       should be the post-scaler height
    DEF_FIELD(31, 16, active_height);
};

DEF_NAMESPACE_REG(Lumvar_ActiveDim, ping, 0x1b274)
DEF_NAMESPACE_REG(Lumvar_ActiveDim, pong, 0x33234)

class MeteringAexp_HistThresh01 :
      public hwreg::RegisterBase<MeteringAexp_HistThresh01, uint32_t> {
public:
    // Histogram threshold for bin 0/1 boundary
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_HistThresh01, ping, 0x1b278)
DEF_NAMESPACE_REG(MeteringAexp_HistThresh01, pong, 0x33238)

class MeteringAexp_HistThresh12 :
      public hwreg::RegisterBase<MeteringAexp_HistThresh12, uint32_t> {
public:
    // Histogram threshold for bin 1/2 boundary
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_HistThresh12, ping, 0x1b27c)
DEF_NAMESPACE_REG(MeteringAexp_HistThresh12, pong, 0x3323c)

class MeteringAexp_HistThresh34 :
      public hwreg::RegisterBase<MeteringAexp_HistThresh34, uint32_t> {
public:
    // Histogram threshold for bin 2/3 boundary
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_HistThresh34, ping, 0x1b280)
DEF_NAMESPACE_REG(MeteringAexp_HistThresh34, pong, 0x33240)

class MeteringAexp_HistThresh45 :
      public hwreg::RegisterBase<MeteringAexp_HistThresh45, uint32_t> {
public:
    // Histogram threshold for bin 3/4 boundary
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_HistThresh45, ping, 0x1b284)
DEF_NAMESPACE_REG(MeteringAexp_HistThresh45, pong, 0x33244)

class MeteringAexp_Hist0 : public hwreg::RegisterBase<MeteringAexp_Hist0, uint32_t> {
public:
    // Normalized histogram results for bin 0
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_Hist0, ping, 0x1b288)
DEF_NAMESPACE_REG(MeteringAexp_Hist0, pong, 0x33248)

class MeteringAexp_Hist1 : public hwreg::RegisterBase<MeteringAexp_Hist1, uint32_t> {
public:
    // Normalized histogram results for bin 1
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_Hist1, ping, 0x1b28c)
DEF_NAMESPACE_REG(MeteringAexp_Hist1, pong, 0x3324c)

class MeteringAexp_Hist3 : public hwreg::RegisterBase<MeteringAexp_Hist3, uint32_t> {
public:
    // Normalized histogram results for bin 3
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_Hist3, ping, 0x1b290)
DEF_NAMESPACE_REG(MeteringAexp_Hist3, pong, 0x33250)

class MeteringAexp_Hist4 : public hwreg::RegisterBase<MeteringAexp_Hist4, uint32_t> {
public:
    // Normalized histogram results for bin 4
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(MeteringAexp_Hist4, ping, 0x1b294)
DEF_NAMESPACE_REG(MeteringAexp_Hist4, pong, 0x33254)

class MeteringAexp_NodesUsed :
      public hwreg::RegisterBase<MeteringAexp_NodesUsed, uint32_t> {
public:
    // Number of active zones horizontally for AE stats collection
    DEF_FIELD(7, 0, nodes_used_horiz);
    // Number of active zones vertically for AE stats collection
    DEF_FIELD(15, 8, nodes_used_vert);
};

DEF_NAMESPACE_REG(MeteringAexp_NodesUsed, ping, 0x1b298)
DEF_NAMESPACE_REG(MeteringAexp_NodesUsed, pong, 0x33258)

class MeteringAwb_StatsMode :
      public hwreg::RegisterBase<MeteringAwb_StatsMode, uint32_t> {
public:
    // Statistics mode: 0 - legacy(G/R,B/R), 1 - current (R/G, B/G)
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_StatsMode, ping, 0x1b29c)
DEF_NAMESPACE_REG(MeteringAwb_StatsMode, pong, 0x3325c)

class MeteringAwb_WhiteLevelAwb :
      public hwreg::RegisterBase<MeteringAwb_WhiteLevelAwb, uint32_t> {
public:
    // Upper limit of valid data for AWB
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_WhiteLevelAwb, ping, 0x1b2a0)
DEF_NAMESPACE_REG(MeteringAwb_WhiteLevelAwb, pong, 0x33260)

class MeteringAwb_BlackLevelAwb :
      public hwreg::RegisterBase<MeteringAwb_BlackLevelAwb, uint32_t> {
public:
    // Lower limit of valid data for AWB
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_BlackLevelAwb, ping, 0x1b2a4)
DEF_NAMESPACE_REG(MeteringAwb_BlackLevelAwb, pong, 0x33264)

class MeteringAwb_CrRefMaxAwb :
      public hwreg::RegisterBase<MeteringAwb_CrRefMaxAwb, uint32_t> {
public:
    // Maximum value of R/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CrRefMaxAwb, ping, 0x1b2a8)
DEF_NAMESPACE_REG(MeteringAwb_CrRefMaxAwb, pong, 0x33268)

class MeteringAwb_CrRefMinAwb :
      public hwreg::RegisterBase<MeteringAwb_CrRefMinAwb, uint32_t> {
public:
    // Minimum value of R/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CrRefMinAwb, ping, 0x1b2ac)
DEF_NAMESPACE_REG(MeteringAwb_CrRefMinAwb, pong, 0x3326c)

class MeteringAwb_CbRefMaxAwb :
      public hwreg::RegisterBase<MeteringAwb_CbRefMaxAwb, uint32_t> {
public:
    // Maximum value of B/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CbRefMaxAwb, ping, 0x1b2b0)
DEF_NAMESPACE_REG(MeteringAwb_CbRefMaxAwb, pong, 0x33270)

class MeteringAwb_CbRefMinAwb :
      public hwreg::RegisterBase<MeteringAwb_CbRefMinAwb, uint32_t> {
public:
    // Minimum value of B/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CbRefMinAwb, ping, 0x1b2b4)
DEF_NAMESPACE_REG(MeteringAwb_CbRefMinAwb, pong, 0x33274)

class MeteringAwb_Rg : public hwreg::RegisterBase<MeteringAwb_Rg, uint32_t> {
public:
    // AWB statistics R/G color ratio output
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_Rg, ping, 0x1b2b8)
DEF_NAMESPACE_REG(MeteringAwb_Rg, pong, 0x33278)

class MeteringAwb_Bg : public hwreg::RegisterBase<MeteringAwb_Bg, uint32_t> {
public:
    // AWB statistics B/G color ratio output
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_Bg, ping, 0x1b2bc)
DEF_NAMESPACE_REG(MeteringAwb_Bg, pong, 0x3327c)

class MeteringAwb_Sum : public hwreg::RegisterBase<MeteringAwb_Sum, uint32_t> {
public:
    // AWB output population.  Number of pixels used for AWB statistics
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_Sum, ping, 0x1b2c0)
DEF_NAMESPACE_REG(MeteringAwb_Sum, pong, 0x33280)

class MeteringAwb_NodesUsed :
      public hwreg::RegisterBase<MeteringAwb_NodesUsed, uint32_t> {
public:
    // Number of active zones horizontally for AWB stats
    DEF_FIELD(7, 0, nodes_used_horiz);
    // Number of active zones vertically for AWB stats
    DEF_FIELD(15, 8, nodes_used_vert);
};

DEF_NAMESPACE_REG(MeteringAwb_NodesUsed, ping, 0x1b2c4)
DEF_NAMESPACE_REG(MeteringAwb_NodesUsed, pong, 0x33284)

class MeteringAwb_CrRefHighAwb :
      public hwreg::RegisterBase<MeteringAwb_CrRefHighAwb, uint32_t> {
public:
    // Maximum value of R/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CrRefHighAwb, ping, 0x1b2c8)
DEF_NAMESPACE_REG(MeteringAwb_CrRefHighAwb, pong, 0x33288)

class MeteringAwb_CrRefLowAwb :
      public hwreg::RegisterBase<MeteringAwb_CrRefLowAwb, uint32_t> {
public:
    // Minimum value of R/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CrRefLowAwb, ping, 0x1b2cc)
DEF_NAMESPACE_REG(MeteringAwb_CrRefLowAwb, pong, 0x3328c)

class MeteringAwb_CbRefHighAwb :
      public hwreg::RegisterBase<MeteringAwb_CbRefHighAwb, uint32_t> {
public:
    // Maximum value of B/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CbRefHighAwb, ping, 0x1b2d0)
DEF_NAMESPACE_REG(MeteringAwb_CbRefHighAwb, pong, 0x33290)

class MeteringAwb_CbRefLowAwb :
      public hwreg::RegisterBase<MeteringAwb_CbRefLowAwb, uint32_t> {
public:
    // Minimum value of B/G for white region
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(MeteringAwb_CbRefLowAwb, ping, 0x1b2d4)
DEF_NAMESPACE_REG(MeteringAwb_CbRefLowAwb, pong, 0x33294)

class MeteringAf_NodesUsed : public hwreg::RegisterBase<MeteringAf_NodesUsed, uint32_t> {
public:
    // Number of active zones horizontally for AF stats
    DEF_FIELD(7, 0, nodes_used_horiz);
    // Number of active zones vertically for AF stats
    DEF_FIELD(15, 8, nodes_used_vert);
};

DEF_NAMESPACE_REG(MeteringAf_NodesUsed, ping, 0x1b720)
DEF_NAMESPACE_REG(MeteringAf_NodesUsed, pong, 0x336e0)

class MeteringAf_Metrics : public hwreg::RegisterBase<MeteringAf_Metrics, uint32_t> {
public:
    // The integrated and normalized measure of contrast for AF
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringAf_Metrics, ping, 0x1b724)
DEF_NAMESPACE_REG(MeteringAf_Metrics, pong, 0x336e4)

class MeteringAf_Active : public hwreg::RegisterBase<MeteringAf_Active, uint32_t> {
public:
    // Active video width for AF module
    DEF_FIELD(15, 0, active_width);
    // Active video height for AF module
    DEF_FIELD(31, 16, active_height);
};

DEF_NAMESPACE_REG(MeteringAf_Active, ping, 0x1b728)
DEF_NAMESPACE_REG(MeteringAf_Active, pong, 0x336e8)

class MeteringAf_KernelSelect :
      public hwreg::RegisterBase<MeteringAf_KernelSelect, uint32_t> {
public:
    // Size of Narrow AF Kernel
    //   0 =   3x3
    //   1 =   7x3
    //   2 =  11x3
    //   3 =  15x3
    DEF_FIELD(1, 0, value);
};

DEF_NAMESPACE_REG(MeteringAf_KernelSelect, ping, 0x1b72c)
DEF_NAMESPACE_REG(MeteringAf_KernelSelect, pong, 0x336ec)

class MeteringHistAexp_Config :
      public hwreg::RegisterBase<MeteringHistAexp_Config, uint32_t> {
public:
    //  Histogram decimation in horizontal direction: 0=every 2nd pixel;
    //   1=every 3rd pixel; 2=every 4th pixel; 3=every 5th pixel; 4=every
    //   8th pixel ; 5+=every 9th pixel
    DEF_FIELD(2, 0, skip_x);
    //  Histogram decimation in vertical direction: 0=every pixel;
    //   1=every 2nd pixel; 2=every 3rd pixel; 3=every 4th pixel; 4=every
    //   5th pixel; 5=every 8th pixel ; 6+=every 9th pixel
    DEF_FIELD(6, 4, skip_y);
    // 0= start from the first column;  1=start from second column
    DEF_BIT(3, offset_x);
    // 0= start from the first row; 1= start from second row
    DEF_BIT(7, offset_y);
};

DEF_NAMESPACE_REG(MeteringHistAexp_Config, ping, 0x1b730)
DEF_NAMESPACE_REG(MeteringHistAexp_Config, pong, 0x336f0)

class MeteringHistAexp_Scale :
      public hwreg::RegisterBase<MeteringHistAexp_Scale, uint32_t> {
public:
    // scale of bottom half of the range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
    DEF_FIELD(3, 0, scale_bottom);
    // scale of top half of the range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
    DEF_FIELD(7, 4, scale_top);
};

DEF_NAMESPACE_REG(MeteringHistAexp_Scale, ping, 0x1b734)
DEF_NAMESPACE_REG(MeteringHistAexp_Scale, pong, 0x336f4)

class MeteringHistAexp_TotalPixels :
      public hwreg::RegisterBase<MeteringHistAexp_TotalPixels, uint32_t> {
public:
    //  Total number of pixels processed (skip x and skip y are taken
    //   into account)
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_TotalPixels, ping, 0x1b738)
DEF_NAMESPACE_REG(MeteringHistAexp_TotalPixels, pong, 0x336f8)

class MeteringHistAexp_CountedPixels :
      public hwreg::RegisterBase<MeteringHistAexp_CountedPixels, uint32_t> {
public:
    // Number of pixels accumulated (with nonzero weight)
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_CountedPixels, ping, 0x1b73c)
DEF_NAMESPACE_REG(MeteringHistAexp_CountedPixels, pong, 0x336fc)

class MeteringHistAexp_PlaneMode :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneMode, uint32_t> {
public:
    //  Plane separation mode (0=Collect all the planes in one histogram,
    //   1=Collect 4 Bayer planes into 4 separate banks, 2=Reserved 2,
    //   3=Reserved 3, 4=Collect odd  x odd  y plane to bank 0, rest to
    //   bank 1, 5=Collect even x odd  y plane to bank 0, rest to bank 1,
    //   6=Collect odd  x even y plane to bank 0, rest to bank 1,
    //   7=Collect even x even y plane to bank 0, rest to bank 1)
    DEF_FIELD(2, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneMode, ping, 0x1b740)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneMode, pong, 0x33700)

class MeteringHistAexp_PlaneTotal0 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneTotal0, uint32_t> {
public:
    // Total pixels processed for plane 0
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal0, ping, 0x1b744)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal0, pong, 0x33704)

class MeteringHistAexp_PlaneTotal1 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneTotal1, uint32_t> {
public:
    // Total pixels processed for plane 1
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal1, ping, 0x1b748)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal1, pong, 0x33708)

class MeteringHistAexp_PlaneTotal2 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneTotal2, uint32_t> {
public:
    // Total pixels processed for plane 2
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal2, ping, 0x1b74c)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal2, pong, 0x3370c)

class MeteringHistAexp_PlaneTotal3 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneTotal3, uint32_t> {
public:
    // Total pixels processed for plane 3
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal3, ping, 0x1b750)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneTotal3, pong, 0x33710)

class MeteringHistAexp_PlaneCounted0 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneCounted0, uint32_t> {
public:
    // Total pixels accumulated for plane 0
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted0, ping, 0x1b754)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted0, pong, 0x33714)

class MeteringHistAexp_PlaneCounted1 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneCounted1, uint32_t> {
public:
    // Total pixels accumulated for plane 1
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted1, ping, 0x1b758)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted1, pong, 0x33718)

class MeteringHistAexp_PlaneCounted2 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneCounted2, uint32_t> {
public:
    // Total pixels accumulated for plane 2
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted2, ping, 0x1b75c)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted2, pong, 0x3371c)

class MeteringHistAexp_PlaneCounted3 :
      public hwreg::RegisterBase<MeteringHistAexp_PlaneCounted3, uint32_t> {
public:
    // Total pixels accumulated for plane 3
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted3, ping, 0x1b760)
DEF_NAMESPACE_REG(MeteringHistAexp_PlaneCounted3, pong, 0x33720)

class MeteringHistAexp_NodesUsed :
      public hwreg::RegisterBase<MeteringHistAexp_NodesUsed, uint32_t> {
public:
    // Number of active zones horizontally for Histogram
    DEF_FIELD(7, 0, nodes_used_horiz);
    // Number of active zones vertically for Histogram
    DEF_FIELD(15, 8, nodes_used_vert);
};

DEF_NAMESPACE_REG(MeteringHistAexp_NodesUsed, ping, 0x1b764)
DEF_NAMESPACE_REG(MeteringHistAexp_NodesUsed, pong, 0x33724)

class MeteringIhist_Config : public hwreg::RegisterBase<MeteringIhist_Config, uint32_t> {
public:
    //  Histogram decimation in horizontal direction: 0=every 2nd pixel;
    //   1=every 3rd pixel; 2=every 4th pixel; 3=every 5th pixel; 4=every
    //   8th pixel ; 5+=every 9th pixel
    DEF_FIELD(2, 0, skip_x);
    //  Histogram decimation in vertical direction: 0=every pixel;
    //   1=every 2nd pixel; 2=every 3rd pixel; 3=every 4th pixel; 4=every
    //   5th pixel; 5=every 8th pixel ; 6+=every 9th pixel
    DEF_FIELD(6, 4, skip_y);
    // 0= start from the first column;  1=start from second column
    DEF_BIT(3, offset_x);
    // 0= start from the first row; 1= start from second row
    DEF_BIT(7, offset_y);
};

DEF_NAMESPACE_REG(MeteringIhist_Config, ping, 0x1bbac)
DEF_NAMESPACE_REG(MeteringIhist_Config, pong, 0x33b6c)

class MeteringIhist_Scale : public hwreg::RegisterBase<MeteringIhist_Scale, uint32_t> {
public:
    // scale of bottom half of the range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
    DEF_FIELD(3, 0, scale_bottom);
    // scale of top half of the range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
    DEF_FIELD(7, 4, scale_top);
};

DEF_NAMESPACE_REG(MeteringIhist_Scale, ping, 0x1bbb0)
DEF_NAMESPACE_REG(MeteringIhist_Scale, pong, 0x33b70)

class MeteringIhist_TotalPixels :
      public hwreg::RegisterBase<MeteringIhist_TotalPixels, uint32_t> {
public:
    //  Total number of pixels processed (skip x and skip y are taken
    //   into account)
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_TotalPixels, ping, 0x1bbb4)
DEF_NAMESPACE_REG(MeteringIhist_TotalPixels, pong, 0x33b74)

class MeteringIhist_CountedPixels :
      public hwreg::RegisterBase<MeteringIhist_CountedPixels, uint32_t> {
public:
    // Number of pixels accumulated (with nonzero weight)
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_CountedPixels, ping, 0x1bbb8)
DEF_NAMESPACE_REG(MeteringIhist_CountedPixels, pong, 0x33b78)

class MeteringIhist_PlaneMode :
      public hwreg::RegisterBase<MeteringIhist_PlaneMode, uint32_t> {
public:
    //  Plane separation mode (0=Collect all the planes in one histogram,
    //   1=Collect 4 Bayer planes into 4 separate banks, 2=Reserved 2,
    //   3=Reserved 3, 4=Collect odd  x odd  y plane to bank 0, rest to
    //   bank 1, 5=Collect even x odd  y plane to bank 0, rest to bank 1,
    //   6=Collect odd  x even y plane to bank 0, rest to bank 1,
    //   7=Collect even x even y plane to bank 0, rest to bank 1)
    DEF_FIELD(2, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneMode, ping, 0x1bbbc)
DEF_NAMESPACE_REG(MeteringIhist_PlaneMode, pong, 0x33b7c)

class MeteringIhist_PlaneTotal0 :
      public hwreg::RegisterBase<MeteringIhist_PlaneTotal0, uint32_t> {
public:
    // Total pixels processed for plane 0
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal0, ping, 0x1bbc0)
DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal0, pong, 0x33b80)

class MeteringIhist_PlaneTotal1 :
      public hwreg::RegisterBase<MeteringIhist_PlaneTotal1, uint32_t> {
public:
    // Total pixels processed for plane 1
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal1, ping, 0x1bbc4)
DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal1, pong, 0x33b84)

class MeteringIhist_PlaneTotal2 :
      public hwreg::RegisterBase<MeteringIhist_PlaneTotal2, uint32_t> {
public:
    // Total pixels processed for plane 2
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal2, ping, 0x1bbc8)
DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal2, pong, 0x33b88)

class MeteringIhist_PlaneTotal3 :
      public hwreg::RegisterBase<MeteringIhist_PlaneTotal3, uint32_t> {
public:
    // Total pixels processed for plane 3
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal3, ping, 0x1bbcc)
DEF_NAMESPACE_REG(MeteringIhist_PlaneTotal3, pong, 0x33b8c)

class MeteringIhist_PlaneCounted0 :
      public hwreg::RegisterBase<MeteringIhist_PlaneCounted0, uint32_t> {
public:
    // Total pixels accumulated for plane 0
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted0, ping, 0x1bbd0)
DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted0, pong, 0x33b90)

class MeteringIhist_PlaneCounted1 :
      public hwreg::RegisterBase<MeteringIhist_PlaneCounted1, uint32_t> {
public:
    // Total pixels accumulated for plane 1
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted1, ping, 0x1bbd4)
DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted1, pong, 0x33b94)

class MeteringIhist_PlaneCounted2 :
      public hwreg::RegisterBase<MeteringIhist_PlaneCounted2, uint32_t> {
public:
    // Total pixels accumulated for plane 2
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted2, ping, 0x1bbd8)
DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted2, pong, 0x33b98)

class MeteringIhist_PlaneCounted3 :
      public hwreg::RegisterBase<MeteringIhist_PlaneCounted3, uint32_t> {
public:
    // Total pixels accumulated for plane 3
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted3, ping, 0x1bbdc)
DEF_NAMESPACE_REG(MeteringIhist_PlaneCounted3, pong, 0x33b9c)

class MeteringIhist_NodesUsed :
      public hwreg::RegisterBase<MeteringIhist_NodesUsed, uint32_t> {
public:
    // Number of active zones horizontally for Histogram
    DEF_FIELD(7, 0, nodes_used_horiz);
    // Number of active zones vertically for Histogram
    DEF_FIELD(15, 8, nodes_used_vert);
};

DEF_NAMESPACE_REG(MeteringIhist_NodesUsed, ping, 0x1bbe0)
DEF_NAMESPACE_REG(MeteringIhist_NodesUsed, pong, 0x33ba0)

class Crop_EnableCrop : public hwreg::RegisterBase<Crop_EnableCrop, uint32_t> {
public:
    // Crop enable: 0=off 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(Crop_EnableCrop, ping::DownScaled, 0x1c19c)
DEF_NAMESPACE_REG(Crop_EnableCrop, ping::FullResolution, 0x1c028)
DEF_NAMESPACE_REG(Crop_EnableCrop, pong::DownScaled, 0x3415c)
DEF_NAMESPACE_REG(Crop_EnableCrop, pong::FullResolution, 0x33fe8)

class Crop_StartX : public hwreg::RegisterBase<Crop_StartX, uint32_t> {
public:
    //  Horizontal offset from left side of image in pixels for output
    //   crop window
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Crop_StartX, ping::DownScaled, 0x1c1a0)
DEF_NAMESPACE_REG(Crop_StartX, ping::FullResolution, 0x1c02c)
DEF_NAMESPACE_REG(Crop_StartX, pong::DownScaled, 0x34160)
DEF_NAMESPACE_REG(Crop_StartX, pong::FullResolution, 0x33fec)

class Crop_StartY : public hwreg::RegisterBase<Crop_StartY, uint32_t> {
public:
    // Vertical offset from top of image in lines for output crop window
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Crop_StartY, ping::DownScaled, 0x1c1a4)
DEF_NAMESPACE_REG(Crop_StartY, ping::FullResolution, 0x1c030)
DEF_NAMESPACE_REG(Crop_StartY, pong::DownScaled, 0x34164)
DEF_NAMESPACE_REG(Crop_StartY, pong::FullResolution, 0x33ff0)

class Crop_SizeX : public hwreg::RegisterBase<Crop_SizeX, uint32_t> {
public:
    // width of output crop window
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Crop_SizeX, ping::DownScaled, 0x1c1a8)
DEF_NAMESPACE_REG(Crop_SizeX, ping::FullResolution, 0x1c034)
DEF_NAMESPACE_REG(Crop_SizeX, pong::DownScaled, 0x34168)
DEF_NAMESPACE_REG(Crop_SizeX, pong::FullResolution, 0x33ff4)

class Crop_SizeY : public hwreg::RegisterBase<Crop_SizeY, uint32_t> {
public:
    // height of output crop window
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Crop_SizeY, ping::DownScaled, 0x1c1ac)
DEF_NAMESPACE_REG(Crop_SizeY, ping::FullResolution, 0x1c038)
DEF_NAMESPACE_REG(Crop_SizeY, pong::DownScaled, 0x3416c)
DEF_NAMESPACE_REG(Crop_SizeY, pong::FullResolution, 0x33ff8)

class Scaler_Irqs : public hwreg::RegisterBase<Scaler_Irqs, uint32_t> {
public:
    // 0 : No timeout
    //       1 : Timeout on frame done
    DEF_BIT(3, timeout_irq);
    // 0 : No underflow
    //       1 : FIFO underflow has occurred
    DEF_BIT(2, underflow_irq);
    // 0 : No overflow
    //       1 : FIFO overflow has occurred
    DEF_BIT(0, overflow_irq);
};

DEF_NAMESPACE_REG(Scaler_Irqs, ping::DownScaled, 0x1c1b0)
DEF_NAMESPACE_REG(Scaler_Irqs, ping::FullResolution, 0x1c03c)
DEF_NAMESPACE_REG(Scaler_Irqs, pong::DownScaled, 0x34170)
DEF_NAMESPACE_REG(Scaler_Irqs, pong::FullResolution, 0x33ffc)

class Scaler_Misc : public hwreg::RegisterBase<Scaler_Misc, uint32_t> {
public:
    // Scaler control
    // IRQ CLR bit
    //   0 : In-active
    //   1 : Clear-off IRQ status to 0
    DEF_BIT(3, clear_alarms);
    // 0 : Timeout disabled.
    //    1 : Timeout enabled.  Automatic frame reset if frame has not
    //         completed after anticipated time.
    DEF_BIT(4, timeout_enable);
    // 0 : Input Field Type = pulse.
    //   1 : Input Field Type = toggle.
    DEF_BIT(5, field_in_toggle_sel);
};

DEF_NAMESPACE_REG(Scaler_Misc, ping::DownScaled, 0x1c1b4)
DEF_NAMESPACE_REG(Scaler_Misc, ping::FullResolution, 0x1c040)
DEF_NAMESPACE_REG(Scaler_Misc, pong::DownScaled, 0x34174)
DEF_NAMESPACE_REG(Scaler_Misc, pong::FullResolution, 0x34000)

class Scaler_Width : public hwreg::RegisterBase<Scaler_Width, uint32_t> {
public:
    // Input frame width in pixels
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Scaler_Width, ping::DownScaled, 0x1c1b8)
DEF_NAMESPACE_REG(Scaler_Width, ping::FullResolution, 0x1c044)
DEF_NAMESPACE_REG(Scaler_Width, pong::DownScaled, 0x34178)
DEF_NAMESPACE_REG(Scaler_Width, pong::FullResolution, 0x34004)

class Scaler_Height : public hwreg::RegisterBase<Scaler_Height, uint32_t> {
public:
    // Input frame height in lines
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Scaler_Height, ping::DownScaled, 0x1c1bc)
DEF_NAMESPACE_REG(Scaler_Height, ping::FullResolution, 0x1c048)
DEF_NAMESPACE_REG(Scaler_Height, pong::DownScaled, 0x3417c)
DEF_NAMESPACE_REG(Scaler_Height, pong::FullResolution, 0x34008)

class Scaler_Owidth : public hwreg::RegisterBase<Scaler_Owidth, uint32_t> {
public:
    // Output frame width in pixels
    DEF_FIELD(12, 0, value);
};

DEF_NAMESPACE_REG(Scaler_Owidth, ping::DownScaled, 0x1c1c0)
DEF_NAMESPACE_REG(Scaler_Owidth, ping::FullResolution, 0x1c04c)
DEF_NAMESPACE_REG(Scaler_Owidth, pong::DownScaled, 0x34180)
DEF_NAMESPACE_REG(Scaler_Owidth, pong::FullResolution, 0x3400c)

class Scaler_Oheight : public hwreg::RegisterBase<Scaler_Oheight, uint32_t> {
public:
    // Output frame height in lines
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(Scaler_Oheight, ping::DownScaled, 0x1c1c4)
DEF_NAMESPACE_REG(Scaler_Oheight, ping::FullResolution, 0x1c050)
DEF_NAMESPACE_REG(Scaler_Oheight, pong::DownScaled, 0x34184)
DEF_NAMESPACE_REG(Scaler_Oheight, pong::FullResolution, 0x34010)

class Scaler_HfiltTinc : public hwreg::RegisterBase<Scaler_HfiltTinc, uint32_t> {
public:
    // Horizontal scaling factor equal to the
    DEF_FIELD(23, 0, value);
};

DEF_NAMESPACE_REG(Scaler_HfiltTinc, ping::DownScaled, 0x1c1c8)
DEF_NAMESPACE_REG(Scaler_HfiltTinc, ping::FullResolution, 0x1c054)
DEF_NAMESPACE_REG(Scaler_HfiltTinc, pong::DownScaled, 0x34188)
DEF_NAMESPACE_REG(Scaler_HfiltTinc, pong::FullResolution, 0x34014)

class Scaler_HfiltCoefset : public hwreg::RegisterBase<Scaler_HfiltCoefset, uint32_t> {
public:
    // HFILT Coeff. control.
    //   HFILT_COEFSET[3:0] - Selects horizontal Coef set for scaler.
    //     0000 : use set 0
    //     0001 : use set 1
    //     ......
    //      1111 : use set 15
    DEF_FIELD(3, 0, value);
};

DEF_NAMESPACE_REG(Scaler_HfiltCoefset, ping::DownScaled, 0x1c1cc)
DEF_NAMESPACE_REG(Scaler_HfiltCoefset, ping::FullResolution, 0x1c058)
DEF_NAMESPACE_REG(Scaler_HfiltCoefset, pong::DownScaled, 0x3418c)
DEF_NAMESPACE_REG(Scaler_HfiltCoefset, pong::FullResolution, 0x34018)

class Scaler_VfiltTinc : public hwreg::RegisterBase<Scaler_VfiltTinc, uint32_t> {
public:
    // VFILT TINC
    DEF_FIELD(23, 0, value);
};

DEF_NAMESPACE_REG(Scaler_VfiltTinc, ping::DownScaled, 0x1c1d0)
DEF_NAMESPACE_REG(Scaler_VfiltTinc, ping::FullResolution, 0x1c05c)
DEF_NAMESPACE_REG(Scaler_VfiltTinc, pong::DownScaled, 0x34190)
DEF_NAMESPACE_REG(Scaler_VfiltTinc, pong::FullResolution, 0x3401c)

class Scaler_VfiltCoefset : public hwreg::RegisterBase<Scaler_VfiltCoefset, uint32_t> {
public:
    // VFILT Coeff. control
    // FILT_COEFSET[3:0] - Selects vertical Coef set for scaler
    //     0000 : use set 0
    //     0001 : use set 1
    //     ......
    //      1111 : use set 15
    DEF_FIELD(3, 0, value);
};

DEF_NAMESPACE_REG(Scaler_VfiltCoefset, ping::DownScaled, 0x1c1d4)
DEF_NAMESPACE_REG(Scaler_VfiltCoefset, ping::FullResolution, 0x1c060)
DEF_NAMESPACE_REG(Scaler_VfiltCoefset, pong::DownScaled, 0x34194)
DEF_NAMESPACE_REG(Scaler_VfiltCoefset, pong::FullResolution, 0x34020)

class GammaRgb_Enable : public hwreg::RegisterBase<GammaRgb_Enable, uint32_t> {
public:
    // Gamma enable: 0=off 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(GammaRgb_Enable, ping::DownScaled, 0x1c1d8)
DEF_NAMESPACE_REG(GammaRgb_Enable, ping::FullResolution, 0x1c064)
DEF_NAMESPACE_REG(GammaRgb_Enable, pong::DownScaled, 0x34198)
DEF_NAMESPACE_REG(GammaRgb_Enable, pong::FullResolution, 0x34024)

class GammaRgb_Gain : public hwreg::RegisterBase<GammaRgb_Gain, uint32_t> {
public:
    // gain applied to the R chanel in 4.8 format
    DEF_FIELD(11, 0, gain_r);
    // gain applied to the G chanel in 4.8 format
    DEF_FIELD(27, 16, gain_g);
};

DEF_NAMESPACE_REG(GammaRgb_Gain, ping::DownScaled, 0x1c1dc)
DEF_NAMESPACE_REG(GammaRgb_Gain, ping::FullResolution, 0x1c068)
DEF_NAMESPACE_REG(GammaRgb_Gain, pong::DownScaled, 0x3419c)
DEF_NAMESPACE_REG(GammaRgb_Gain, pong::FullResolution, 0x34028)

class GammaRgb_GainB : public hwreg::RegisterBase<GammaRgb_GainB, uint32_t> {
public:
    // gain applied to the B chanel in 4.8 format
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(GammaRgb_GainB, ping::DownScaled, 0x1c1e0)
DEF_NAMESPACE_REG(GammaRgb_GainB, ping::FullResolution, 0x1c06c)
DEF_NAMESPACE_REG(GammaRgb_GainB, pong::DownScaled, 0x341a0)
DEF_NAMESPACE_REG(GammaRgb_GainB, pong::FullResolution, 0x3402c)

class GammaRgb_Offset : public hwreg::RegisterBase<GammaRgb_Offset, uint32_t> {
public:
    // Offset subtracted from the R chanel
    DEF_FIELD(11, 0, offset_r);
    // Offset subtracted from the G chanel
    DEF_FIELD(27, 16, offset_g);
};

DEF_NAMESPACE_REG(GammaRgb_Offset, ping::DownScaled, 0x1c1e4)
DEF_NAMESPACE_REG(GammaRgb_Offset, ping::FullResolution, 0x1c070)
DEF_NAMESPACE_REG(GammaRgb_Offset, pong::DownScaled, 0x341a4)
DEF_NAMESPACE_REG(GammaRgb_Offset, pong::FullResolution, 0x34030)

class GammaRgb_OffsetB : public hwreg::RegisterBase<GammaRgb_OffsetB, uint32_t> {
public:
    // Offset subtracted from the B chanel
    DEF_FIELD(11, 0, value);
};

DEF_NAMESPACE_REG(GammaRgb_OffsetB, ping::DownScaled, 0x1c1e8)
DEF_NAMESPACE_REG(GammaRgb_OffsetB, ping::FullResolution, 0x1c074)
DEF_NAMESPACE_REG(GammaRgb_OffsetB, pong::DownScaled, 0x341a8)
DEF_NAMESPACE_REG(GammaRgb_OffsetB, pong::FullResolution, 0x34034)

class Sharpen_Enable : public hwreg::RegisterBase<Sharpen_Enable, uint32_t> {
public:
    // Sharpening enable: 0=off, 1=on
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(Sharpen_Enable, ping::DownScaled, 0x1c1ec)
DEF_NAMESPACE_REG(Sharpen_Enable, ping::FullResolution, 0x1c078)
DEF_NAMESPACE_REG(Sharpen_Enable, pong::DownScaled, 0x341ac)
DEF_NAMESPACE_REG(Sharpen_Enable, pong::FullResolution, 0x34038)

class Sharpen_Strength : public hwreg::RegisterBase<Sharpen_Strength, uint32_t> {
public:
    // Controls strength of sharpening effect. u5.4
    DEF_FIELD(8, 0, value);
};

DEF_NAMESPACE_REG(Sharpen_Strength, ping::DownScaled, 0x1c1f0)
DEF_NAMESPACE_REG(Sharpen_Strength, ping::FullResolution, 0x1c07c)
DEF_NAMESPACE_REG(Sharpen_Strength, pong::DownScaled, 0x341b0)
DEF_NAMESPACE_REG(Sharpen_Strength, pong::FullResolution, 0x3403c)

class Sharpen_Misc : public hwreg::RegisterBase<Sharpen_Misc, uint32_t> {
public:
    DEF_FIELD(7, 0, control_r);
    DEF_FIELD(15, 8, control_b);
    //  Alpha blending of undershoot and overshoot u0.7, 0 = only
    //   unsershoot, 255 = only overshoot
    DEF_FIELD(23, 16, alpha_undershoot);
};

DEF_NAMESPACE_REG(Sharpen_Misc, ping::DownScaled, 0x1c1f4)
DEF_NAMESPACE_REG(Sharpen_Misc, ping::FullResolution, 0x1c080)
DEF_NAMESPACE_REG(Sharpen_Misc, pong::DownScaled, 0x341b4)
DEF_NAMESPACE_REG(Sharpen_Misc, pong::FullResolution, 0x34040)

class Sharpen_Luma1 : public hwreg::RegisterBase<Sharpen_Luma1, uint32_t> {
public:
    // Luma threshold below this value, no sharpening will be applied.
    DEF_FIELD(9, 0, luma_thresh_low);
    // Luma offset (min value) of thre region of less than Luma Thresh Low.
    DEF_FIELD(23, 16, luma_offset_low);
};

DEF_NAMESPACE_REG(Sharpen_Luma1, ping::DownScaled, 0x1c1f8)
DEF_NAMESPACE_REG(Sharpen_Luma1, ping::FullResolution, 0x1c084)
DEF_NAMESPACE_REG(Sharpen_Luma1, pong::DownScaled, 0x341b8)
DEF_NAMESPACE_REG(Sharpen_Luma1, pong::FullResolution, 0x34044)

class Sharpen_Luma2 : public hwreg::RegisterBase<Sharpen_Luma2, uint32_t> {
public:
    // Luma linear threshold slope at dark luminance region
    DEF_FIELD(15, 0, luma_slope_low);
    // Luma threshold above this value, sharpening level will be dicreased.
    DEF_FIELD(25, 16, luma_thresh_high);
};

DEF_NAMESPACE_REG(Sharpen_Luma2, ping::DownScaled, 0x1c1fc)
DEF_NAMESPACE_REG(Sharpen_Luma2, ping::FullResolution, 0x1c088)
DEF_NAMESPACE_REG(Sharpen_Luma2, pong::DownScaled, 0x341bc)
DEF_NAMESPACE_REG(Sharpen_Luma2, pong::FullResolution, 0x34048)

class Sharpen_Luma3 : public hwreg::RegisterBase<Sharpen_Luma3, uint32_t> {
public:
    // Luma offset (min value) of thre region of more than Luma Thresh High.
    DEF_FIELD(7, 0, luma_offset_high);
    // Luma linear threshold slope at bright luminance region
    DEF_FIELD(31, 16, luma_slope_high);
};

DEF_NAMESPACE_REG(Sharpen_Luma3, ping::DownScaled, 0x1c200)
DEF_NAMESPACE_REG(Sharpen_Luma3, ping::FullResolution, 0x1c08c)
DEF_NAMESPACE_REG(Sharpen_Luma3, pong::DownScaled, 0x341c0)
DEF_NAMESPACE_REG(Sharpen_Luma3, pong::FullResolution, 0x3404c)

class Sharpen_Clip : public hwreg::RegisterBase<Sharpen_Clip, uint32_t> {
public:
    //  clips sharpening mask of max value. This will control overshoot.
    //   U0.14. (0 ~ 16383)
    DEF_FIELD(13, 0, clip_str_max);
    //  clips sharpening mask of min value. This will control undershoot.
    //   U0.14. It is used as negative value. (0 ~ -16383)
    DEF_FIELD(29, 16, clip_str_min);
};

DEF_NAMESPACE_REG(Sharpen_Clip, ping::DownScaled, 0x1c204)
DEF_NAMESPACE_REG(Sharpen_Clip, ping::FullResolution, 0x1c090)
DEF_NAMESPACE_REG(Sharpen_Clip, pong::DownScaled, 0x341c4)
DEF_NAMESPACE_REG(Sharpen_Clip, pong::FullResolution, 0x34050)

class Sharpen_Debug : public hwreg::RegisterBase<Sharpen_Debug, uint32_t> {
public:
    //  To support different debug output. 0 = normal operation, 1 =
    //   luma, 2 = sharpening mask
    DEF_FIELD(3, 0, value);
};

DEF_NAMESPACE_REG(Sharpen_Debug, ping::DownScaled, 0x1c208)
DEF_NAMESPACE_REG(Sharpen_Debug, ping::FullResolution, 0x1c094)
DEF_NAMESPACE_REG(Sharpen_Debug, pong::DownScaled, 0x341c8)
DEF_NAMESPACE_REG(Sharpen_Debug, pong::FullResolution, 0x34054)

class CsConv_Enable : public hwreg::RegisterBase<CsConv_Enable, uint32_t> {
public:
    // Color matrix enable: 0=off 1=on
    DEF_BIT(0, enable_matrix);
    // Filter enable: 0=off 1=on
    DEF_BIT(1, enable_filter);
    // Horizontal Downsampling Enable: 0=off 1=on
    DEF_BIT(2, enable_horizontal_downsample);
    // Vertical Downsampling Enable: 0=off 1=on
    DEF_BIT(3, enable_vertical_downsample);
};

DEF_NAMESPACE_REG(CsConv_Enable, ping::DownScaled, 0x1c20c)
DEF_NAMESPACE_REG(CsConv_Enable, ping::FullResolution, 0x1c098)
DEF_NAMESPACE_REG(CsConv_Enable, pong::DownScaled, 0x341cc)
DEF_NAMESPACE_REG(CsConv_Enable, pong::FullResolution, 0x34058)

class CsConv_Coefft11 : public hwreg::RegisterBase<CsConv_Coefft11, uint32_t> {
public:
    // Matrix coefficient for R-Y multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft11, ping::DownScaled, 0x1c210)
DEF_NAMESPACE_REG(CsConv_Coefft11, ping::FullResolution, 0x1c09c)
DEF_NAMESPACE_REG(CsConv_Coefft11, pong::DownScaled, 0x341d0)
DEF_NAMESPACE_REG(CsConv_Coefft11, pong::FullResolution, 0x3405c)

class CsConv_Coefft12 : public hwreg::RegisterBase<CsConv_Coefft12, uint32_t> {
public:
    // Matrix coefficient for G-Y multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft12, ping::DownScaled, 0x1c214)
DEF_NAMESPACE_REG(CsConv_Coefft12, ping::FullResolution, 0x1c0a0)
DEF_NAMESPACE_REG(CsConv_Coefft12, pong::DownScaled, 0x341d4)
DEF_NAMESPACE_REG(CsConv_Coefft12, pong::FullResolution, 0x34060)

class CsConv_Coefft13 : public hwreg::RegisterBase<CsConv_Coefft13, uint32_t> {
public:
    // Matrix coefficient for B-Y multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft13, ping::DownScaled, 0x1c218)
DEF_NAMESPACE_REG(CsConv_Coefft13, ping::FullResolution, 0x1c0a4)
DEF_NAMESPACE_REG(CsConv_Coefft13, pong::DownScaled, 0x341d8)
DEF_NAMESPACE_REG(CsConv_Coefft13, pong::FullResolution, 0x34064)

class CsConv_Coefft21 : public hwreg::RegisterBase<CsConv_Coefft21, uint32_t> {
public:
    // Matrix coefficient for R-Cb multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft21, ping::DownScaled, 0x1c21c)
DEF_NAMESPACE_REG(CsConv_Coefft21, ping::FullResolution, 0x1c0a8)
DEF_NAMESPACE_REG(CsConv_Coefft21, pong::DownScaled, 0x341dc)
DEF_NAMESPACE_REG(CsConv_Coefft21, pong::FullResolution, 0x34068)

class CsConv_Coefft22 : public hwreg::RegisterBase<CsConv_Coefft22, uint32_t> {
public:
    // Matrix coefficient for G-Cb multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft22, ping::DownScaled, 0x1c220)
DEF_NAMESPACE_REG(CsConv_Coefft22, ping::FullResolution, 0x1c0ac)
DEF_NAMESPACE_REG(CsConv_Coefft22, pong::DownScaled, 0x341e0)
DEF_NAMESPACE_REG(CsConv_Coefft22, pong::FullResolution, 0x3406c)

class CsConv_Coefft23 : public hwreg::RegisterBase<CsConv_Coefft23, uint32_t> {
public:
    // Matrix coefficient for B-Cb multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft23, ping::DownScaled, 0x1c224)
DEF_NAMESPACE_REG(CsConv_Coefft23, ping::FullResolution, 0x1c0b0)
DEF_NAMESPACE_REG(CsConv_Coefft23, pong::DownScaled, 0x341e4)
DEF_NAMESPACE_REG(CsConv_Coefft23, pong::FullResolution, 0x34070)

class CsConv_Coefft31 : public hwreg::RegisterBase<CsConv_Coefft31, uint32_t> {
public:
    // Matrix coefficient for R-Cr multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft31, ping::DownScaled, 0x1c228)
DEF_NAMESPACE_REG(CsConv_Coefft31, ping::FullResolution, 0x1c0b4)
DEF_NAMESPACE_REG(CsConv_Coefft31, pong::DownScaled, 0x341e8)
DEF_NAMESPACE_REG(CsConv_Coefft31, pong::FullResolution, 0x34074)

class CsConv_Coefft32 : public hwreg::RegisterBase<CsConv_Coefft32, uint32_t> {
public:
    // Matrix coefficient for G-Cr multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft32, ping::DownScaled, 0x1c22c)
DEF_NAMESPACE_REG(CsConv_Coefft32, ping::FullResolution, 0x1c0b8)
DEF_NAMESPACE_REG(CsConv_Coefft32, pong::DownScaled, 0x341ec)
DEF_NAMESPACE_REG(CsConv_Coefft32, pong::FullResolution, 0x34078)

class CsConv_Coefft33 : public hwreg::RegisterBase<CsConv_Coefft33, uint32_t> {
public:
    // Matrix coefficient for B-Cr multiplier
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(CsConv_Coefft33, ping::DownScaled, 0x1c230)
DEF_NAMESPACE_REG(CsConv_Coefft33, ping::FullResolution, 0x1c0bc)
DEF_NAMESPACE_REG(CsConv_Coefft33, pong::DownScaled, 0x341f0)
DEF_NAMESPACE_REG(CsConv_Coefft33, pong::FullResolution, 0x3407c)

class CsConv_CoefftO1 : public hwreg::RegisterBase<CsConv_CoefftO1, uint32_t> {
public:
    // Offset for Y
    DEF_FIELD(10, 0, value);
};

DEF_NAMESPACE_REG(CsConv_CoefftO1, ping::DownScaled, 0x1c234)
DEF_NAMESPACE_REG(CsConv_CoefftO1, ping::FullResolution, 0x1c0c0)
DEF_NAMESPACE_REG(CsConv_CoefftO1, pong::DownScaled, 0x341f4)
DEF_NAMESPACE_REG(CsConv_CoefftO1, pong::FullResolution, 0x34080)

class CsConv_CoefftO2 : public hwreg::RegisterBase<CsConv_CoefftO2, uint32_t> {
public:
    // Offset for Cb
    DEF_FIELD(10, 0, value);
};

DEF_NAMESPACE_REG(CsConv_CoefftO2, ping::DownScaled, 0x1c238)
DEF_NAMESPACE_REG(CsConv_CoefftO2, ping::FullResolution, 0x1c0c4)
DEF_NAMESPACE_REG(CsConv_CoefftO2, pong::DownScaled, 0x341f8)
DEF_NAMESPACE_REG(CsConv_CoefftO2, pong::FullResolution, 0x34084)

class CsConv_CoefftO3 : public hwreg::RegisterBase<CsConv_CoefftO3, uint32_t> {
public:
    // Offset for Cr
    DEF_FIELD(10, 0, value);
};

DEF_NAMESPACE_REG(CsConv_CoefftO3, ping::DownScaled, 0x1c23c)
DEF_NAMESPACE_REG(CsConv_CoefftO3, ping::FullResolution, 0x1c0c8)
DEF_NAMESPACE_REG(CsConv_CoefftO3, pong::DownScaled, 0x341fc)
DEF_NAMESPACE_REG(CsConv_CoefftO3, pong::FullResolution, 0x34088)

class CsConv_ClipMinY : public hwreg::RegisterBase<CsConv_ClipMinY, uint32_t> {
public:
    // Minimal value for Y.  Values below this are clipped.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_ClipMinY, ping::DownScaled, 0x1c240)
DEF_NAMESPACE_REG(CsConv_ClipMinY, ping::FullResolution, 0x1c0cc)
DEF_NAMESPACE_REG(CsConv_ClipMinY, pong::DownScaled, 0x34200)
DEF_NAMESPACE_REG(CsConv_ClipMinY, pong::FullResolution, 0x3408c)

class CsConv_ClipMaxY : public hwreg::RegisterBase<CsConv_ClipMaxY, uint32_t> {
public:
    // Maximal value for Y.  Values above this are clipped.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_ClipMaxY, ping::DownScaled, 0x1c244)
DEF_NAMESPACE_REG(CsConv_ClipMaxY, ping::FullResolution, 0x1c0d0)
DEF_NAMESPACE_REG(CsConv_ClipMaxY, pong::DownScaled, 0x34204)
DEF_NAMESPACE_REG(CsConv_ClipMaxY, pong::FullResolution, 0x34090)

class CsConv_ClipMinUv : public hwreg::RegisterBase<CsConv_ClipMinUv, uint32_t> {
public:
    // Minimal value for Cb, Cr.  Values below this are clipped.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_ClipMinUv, ping::DownScaled, 0x1c248)
DEF_NAMESPACE_REG(CsConv_ClipMinUv, ping::FullResolution, 0x1c0d4)
DEF_NAMESPACE_REG(CsConv_ClipMinUv, pong::DownScaled, 0x34208)
DEF_NAMESPACE_REG(CsConv_ClipMinUv, pong::FullResolution, 0x34094)

class CsConv_ClipMaxUv : public hwreg::RegisterBase<CsConv_ClipMaxUv, uint32_t> {
public:
    // Maximal value for Cb, Cr.  Values above this are clipped.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_ClipMaxUv, ping::DownScaled, 0x1c24c)
DEF_NAMESPACE_REG(CsConv_ClipMaxUv, ping::FullResolution, 0x1c0d8)
DEF_NAMESPACE_REG(CsConv_ClipMaxUv, pong::DownScaled, 0x3420c)
DEF_NAMESPACE_REG(CsConv_ClipMaxUv, pong::FullResolution, 0x34098)

class CsConv_DataMaskRy : public hwreg::RegisterBase<CsConv_DataMaskRy, uint32_t> {
public:
    //  Data mask for channel 1 (R or Y).  Bit-wise and of this value and
    //   video data.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_DataMaskRy, ping::DownScaled, 0x1c250)
DEF_NAMESPACE_REG(CsConv_DataMaskRy, ping::FullResolution, 0x1c0dc)
DEF_NAMESPACE_REG(CsConv_DataMaskRy, pong::DownScaled, 0x34210)
DEF_NAMESPACE_REG(CsConv_DataMaskRy, pong::FullResolution, 0x3409c)

class CsConv_DataMaskGu : public hwreg::RegisterBase<CsConv_DataMaskGu, uint32_t> {
public:
    //  Data mask for channel 2 (G or U).  Bit-wise and of this value and
    //   video data.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_DataMaskGu, ping::DownScaled, 0x1c254)
DEF_NAMESPACE_REG(CsConv_DataMaskGu, ping::FullResolution, 0x1c0e0)
DEF_NAMESPACE_REG(CsConv_DataMaskGu, pong::DownScaled, 0x34214)
DEF_NAMESPACE_REG(CsConv_DataMaskGu, pong::FullResolution, 0x340a0)

class CsConv_DataMaskBv : public hwreg::RegisterBase<CsConv_DataMaskBv, uint32_t> {
public:
    //  Data mask for channel 3 (B or V).  Bit-wise and of this value and
    //   video data.
    DEF_FIELD(9, 0, value);
};

DEF_NAMESPACE_REG(CsConv_DataMaskBv, ping::DownScaled, 0x1c258)
DEF_NAMESPACE_REG(CsConv_DataMaskBv, ping::FullResolution, 0x1c0e4)
DEF_NAMESPACE_REG(CsConv_DataMaskBv, pong::DownScaled, 0x34218)
DEF_NAMESPACE_REG(CsConv_DataMaskBv, pong::FullResolution, 0x340a4)

class CsConvDither_Config : public hwreg::RegisterBase<CsConvDither_Config, uint32_t> {
public:
    //  0= dither to 9 bits; 1=dither to 8 bits; 2=dither to 7 bits;
    //      3=dither to 6 bits
    DEF_FIELD(2, 1, dither_amount);
    // Enables dithering module
    DEF_BIT(0, enable_dither);
    // 0= output is LSB aligned; 1=output is MSB aligned
    DEF_BIT(4, shift_mode);
};

DEF_NAMESPACE_REG(CsConvDither_Config, ping::DownScaled, 0x1c25c)
DEF_NAMESPACE_REG(CsConvDither_Config, ping::FullResolution, 0x1c0e8)
DEF_NAMESPACE_REG(CsConvDither_Config, pong::DownScaled, 0x3421c)
DEF_NAMESPACE_REG(CsConvDither_Config, pong::FullResolution, 0x340a8)

class DmaWriter_Misc : public hwreg::RegisterBase<DmaWriter_Misc, uint32_t> {
public:
    // Base DMA packing mode for RGB/RAW/YUV etc (see ISP guide)
    DEF_FIELD(4, 0, base_mode);
    //  Plane select for planar base modes.  Only used if planar outputs
    //   required.  Not used.  Should be set to 0
    DEF_FIELD(7, 6, plane_select);
    //  0 = All frames are written(after frame_write_on= 1), 1= only 1st
    //       frame written ( after frame_write_on =1)
    DEF_BIT(8, single_frame);
    //  0 = no frames written(when switched from 1, current frame
    //       completes writing before stopping),
    // 1= write frame(s) (write single or continous frame(s) )
    DEF_BIT(9, frame_write_on);
    //  0 = dont wait for axi transaction completion at end of frame(just
    //       all transfers accepted). 1 = wait for all transactions
    //       completed
    DEF_BIT(11, axi_xact_comp);
};

DEF_NAMESPACE_REG(DmaWriter_Misc, ping::DownScaled::Primary, 0x1c260)
DEF_NAMESPACE_REG(DmaWriter_Misc, ping::DownScaled::Uv, 0x1c2b8)
DEF_NAMESPACE_REG(DmaWriter_Misc, ping::FullResolution::Primary, 0x1c0ec)
DEF_NAMESPACE_REG(DmaWriter_Misc, ping::FullResolution::Uv, 0x1c144)
DEF_NAMESPACE_REG(DmaWriter_Misc, pong::DownScaled::Primary, 0x34220)
DEF_NAMESPACE_REG(DmaWriter_Misc, pong::DownScaled::Uv, 0x34278)
DEF_NAMESPACE_REG(DmaWriter_Misc, pong::FullResolution::Primary, 0x340ac)
DEF_NAMESPACE_REG(DmaWriter_Misc, pong::FullResolution::Uv, 0x34104)

class DmaWriter_ActiveDim : public hwreg::RegisterBase<DmaWriter_ActiveDim, uint32_t> {
public:
    // Active video width in pixels 128-8000
    DEF_FIELD(15, 0, active_width);
    // Active video height in lines 128-8000
    DEF_FIELD(31, 16, active_height);
};

DEF_NAMESPACE_REG(DmaWriter_ActiveDim, ping::DownScaled::Primary, 0x1c264)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, ping::DownScaled::Uv, 0x1c2bc)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, ping::FullResolution::Primary, 0x1c0f0)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, ping::FullResolution::Uv, 0x1c148)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, pong::DownScaled::Primary, 0x34224)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, pong::DownScaled::Uv, 0x3427c)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, pong::FullResolution::Primary, 0x340b0)
DEF_NAMESPACE_REG(DmaWriter_ActiveDim, pong::FullResolution::Uv, 0x34108)

class DmaWriter_Bank0Base : public hwreg::RegisterBase<DmaWriter_Bank0Base, uint32_t> {
public:
    // bank 0 base address for frame buffer, should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_Bank0Base, ping::DownScaled::Primary, 0x1c268)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, ping::DownScaled::Uv, 0x1c2c0)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, ping::FullResolution::Primary, 0x1c0f4)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, ping::FullResolution::Uv, 0x1c14c)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, pong::DownScaled::Primary, 0x34228)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, pong::DownScaled::Uv, 0x34280)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, pong::FullResolution::Primary, 0x340b4)
DEF_NAMESPACE_REG(DmaWriter_Bank0Base, pong::FullResolution::Uv, 0x3410c)

class DmaWriter_Bank1Base : public hwreg::RegisterBase<DmaWriter_Bank1Base, uint32_t> {
public:
    // bank 1 base address for frame buffer, should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_Bank1Base, ping::DownScaled::Primary, 0x1c26c)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, ping::DownScaled::Uv, 0x1c2c4)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, ping::FullResolution::Primary, 0x1c0f8)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, ping::FullResolution::Uv, 0x1c150)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, pong::DownScaled::Primary, 0x3422c)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, pong::DownScaled::Uv, 0x34284)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, pong::FullResolution::Primary, 0x340b8)
DEF_NAMESPACE_REG(DmaWriter_Bank1Base, pong::FullResolution::Uv, 0x34110)

class DmaWriter_Bank2Base : public hwreg::RegisterBase<DmaWriter_Bank2Base, uint32_t> {
public:
    // bank 2 base address for frame buffer, should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_Bank2Base, ping::DownScaled::Primary, 0x1c270)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, ping::DownScaled::Uv, 0x1c2c8)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, ping::FullResolution::Primary, 0x1c0fc)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, ping::FullResolution::Uv, 0x1c154)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, pong::DownScaled::Primary, 0x34230)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, pong::DownScaled::Uv, 0x34288)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, pong::FullResolution::Primary, 0x340bc)
DEF_NAMESPACE_REG(DmaWriter_Bank2Base, pong::FullResolution::Uv, 0x34114)

class DmaWriter_Bank3Base : public hwreg::RegisterBase<DmaWriter_Bank3Base, uint32_t> {
public:
    // bank 3 base address for frame buffer, should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_Bank3Base, ping::DownScaled::Primary, 0x1c274)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, ping::DownScaled::Uv, 0x1c2cc)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, ping::FullResolution::Primary, 0x1c100)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, ping::FullResolution::Uv, 0x1c158)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, pong::DownScaled::Primary, 0x34234)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, pong::DownScaled::Uv, 0x3428c)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, pong::FullResolution::Primary, 0x340c0)
DEF_NAMESPACE_REG(DmaWriter_Bank3Base, pong::FullResolution::Uv, 0x34118)

class DmaWriter_Bank4Base : public hwreg::RegisterBase<DmaWriter_Bank4Base, uint32_t> {
public:
    // bank 4 base address for frame buffer, should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_Bank4Base, ping::DownScaled::Primary, 0x1c278)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, ping::DownScaled::Uv, 0x1c2d0)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, ping::FullResolution::Primary, 0x1c104)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, ping::FullResolution::Uv, 0x1c15c)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, pong::DownScaled::Primary, 0x34238)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, pong::DownScaled::Uv, 0x34290)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, pong::FullResolution::Primary, 0x340c4)
DEF_NAMESPACE_REG(DmaWriter_Bank4Base, pong::FullResolution::Uv, 0x3411c)

class DmaWriter_Bank : public hwreg::RegisterBase<DmaWriter_Bank, uint32_t> {
public:
    //  highest bank*_base to use for frame writes before recycling to
    //   bank0_base, only 0 to 4 are valid
    DEF_FIELD(2, 0, max_bank);
    //  0 = normal operation, 1= restart bank counter to bank0 for next
    //       frame write
    DEF_BIT(3, bank0_restart);
};

DEF_NAMESPACE_REG(DmaWriter_Bank, ping::DownScaled::Primary, 0x1c27c)
DEF_NAMESPACE_REG(DmaWriter_Bank, ping::DownScaled::Uv, 0x1c2d4)
DEF_NAMESPACE_REG(DmaWriter_Bank, ping::FullResolution::Primary, 0x1c108)
DEF_NAMESPACE_REG(DmaWriter_Bank, ping::FullResolution::Uv, 0x1c160)
DEF_NAMESPACE_REG(DmaWriter_Bank, pong::DownScaled::Primary, 0x3423c)
DEF_NAMESPACE_REG(DmaWriter_Bank, pong::DownScaled::Uv, 0x34294)
DEF_NAMESPACE_REG(DmaWriter_Bank, pong::FullResolution::Primary, 0x340c8)
DEF_NAMESPACE_REG(DmaWriter_Bank, pong::FullResolution::Uv, 0x34120)

class DmaWriter_LineOffset : public hwreg::RegisterBase<DmaWriter_LineOffset, uint32_t> {
public:
    //  Indicates the offset in bytes from the start of one line to the
    //   next line.
    //    This value should be equal to or larger than one line of image
    //     data and should be word-aligned
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_LineOffset, ping::DownScaled::Primary, 0x1c280)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, ping::DownScaled::Uv, 0x1c2d8)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, ping::FullResolution::Primary, 0x1c10c)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, ping::FullResolution::Uv, 0x1c164)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, pong::DownScaled::Primary, 0x34240)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, pong::DownScaled::Uv, 0x34298)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, pong::FullResolution::Primary, 0x340cc)
DEF_NAMESPACE_REG(DmaWriter_LineOffset, pong::FullResolution::Uv, 0x34124)

class DmaWriter_WBank : public hwreg::RegisterBase<DmaWriter_WBank, uint32_t> {
public:
    //  write bank currently active. valid values =0-4. updated at start
    //   of frame write
    DEF_FIELD(3, 1, wbank_curr);
    //  1 = wbank_curr is being written to. Goes high at start of writes,
    //       low at last write transfer/completion on axi.
    DEF_BIT(0, wbank_active);
};

DEF_NAMESPACE_REG(DmaWriter_WBank, ping::DownScaled::Primary, 0x1c284)
DEF_NAMESPACE_REG(DmaWriter_WBank, ping::DownScaled::Uv, 0x1c2dc)
DEF_NAMESPACE_REG(DmaWriter_WBank, ping::FullResolution::Primary, 0x1c110)
DEF_NAMESPACE_REG(DmaWriter_WBank, ping::FullResolution::Uv, 0x1c168)
DEF_NAMESPACE_REG(DmaWriter_WBank, pong::DownScaled::Primary, 0x34244)
DEF_NAMESPACE_REG(DmaWriter_WBank, pong::DownScaled::Uv, 0x3429c)
DEF_NAMESPACE_REG(DmaWriter_WBank, pong::FullResolution::Primary, 0x340d0)
DEF_NAMESPACE_REG(DmaWriter_WBank, pong::FullResolution::Uv, 0x34128)

class DmaWriter_FrameCount : public hwreg::RegisterBase<DmaWriter_FrameCount, uint32_t> {
public:
    //  count of incomming frames (starts) to vdma_writer on video input,
    //   non resetable, rolls over, updates at pixel 1 of new frame on
    //   video in
    DEF_FIELD(15, 0, frame_icount);
    //  count of outgoing frame writes (starts) from vdma_writer sent to
    //   AXI output, non resetable, rolls over, updates at pixel 1 of new
    //   frame on video in
    DEF_FIELD(31, 16, frame_wcount);
};

DEF_NAMESPACE_REG(DmaWriter_FrameCount, ping::DownScaled::Primary, 0x1c290)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, ping::DownScaled::Uv, 0x1c2e8)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, ping::FullResolution::Primary, 0x1c11c)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, ping::FullResolution::Uv, 0x1c174)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, pong::DownScaled::Primary, 0x34250)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, pong::DownScaled::Uv, 0x342a8)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, pong::FullResolution::Primary, 0x340dc)
DEF_NAMESPACE_REG(DmaWriter_FrameCount, pong::FullResolution::Uv, 0x34134)

class DmaWriter_Failures : public hwreg::RegisterBase<DmaWriter_Failures, uint32_t> {
public:
    // clearable alarm, high to indicate bad  bresp captured
    DEF_BIT(0, axi_fail_bresp);
    // clearable alarm, high when awmaxwait_limit reached
    DEF_BIT(1, axi_fail_awmaxwait);
    // clearable alarm, high when wmaxwait_limit reached
    DEF_BIT(2, axi_fail_wmaxwait);
    // clearable alarm, high when wxact_ostand_limit reached
    DEF_BIT(3, axi_fail_wxact_ostand);
    // clearable alarm, high to indicate mismatched active_width detected
    DEF_BIT(4, vi_fail_active_width);
    //  clearable alarm, high to indicate mismatched active_height
    //   detected ( also raised on missing field!)
    DEF_BIT(5, vi_fail_active_height);
    // clearable alarm, high to indicate interline blanking below min
    DEF_BIT(6, vi_fail_interline_blanks);
    // clearable alarm, high to indicate interframe blanking below min
    DEF_BIT(7, vi_fail_interframe_blanks);
    //  active high, problem found on video port(s) ( active width/height
    //   or interline/frame blanks failure)
    DEF_BIT(8, video_alarm);
};

DEF_NAMESPACE_REG(DmaWriter_Failures, ping::DownScaled::Primary, 0x1c298)
DEF_NAMESPACE_REG(DmaWriter_Failures, ping::DownScaled::Uv, 0x1c2f0)
DEF_NAMESPACE_REG(DmaWriter_Failures, ping::FullResolution::Primary, 0x1c124)
DEF_NAMESPACE_REG(DmaWriter_Failures, ping::FullResolution::Uv, 0x1c17c)
DEF_NAMESPACE_REG(DmaWriter_Failures, pong::DownScaled::Primary, 0x34258)
DEF_NAMESPACE_REG(DmaWriter_Failures, pong::DownScaled::Uv, 0x342b0)
DEF_NAMESPACE_REG(DmaWriter_Failures, pong::FullResolution::Primary, 0x340e4)
DEF_NAMESPACE_REG(DmaWriter_Failures, pong::FullResolution::Uv, 0x3413c)

class DmaWriter_BlkStatus : public hwreg::RegisterBase<DmaWriter_BlkStatus, uint32_t> {
public:
    // block status output (reserved)
    // -- blk_status(0) = wfifo_fail_full
    // -- blk_status(1) = wfifo_fail_empty
    // -- blk_status(4) = pack_fail_overflow
    // -- blk_status(24) = intw_fail_user_intfc_sig
    // -- blk_status(others) =  zero
    DEF_FIELD(31, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_BlkStatus, ping::DownScaled::Primary, 0x1c29c)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, ping::DownScaled::Uv, 0x1c2f4)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, ping::FullResolution::Primary, 0x1c128)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, ping::FullResolution::Uv, 0x1c180)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, pong::DownScaled::Primary, 0x3425c)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, pong::DownScaled::Uv, 0x342b4)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, pong::FullResolution::Primary, 0x340e8)
DEF_NAMESPACE_REG(DmaWriter_BlkStatus, pong::FullResolution::Uv, 0x34140)

class DmaWriter_LinesWrapped :
      public hwreg::RegisterBase<DmaWriter_LinesWrapped, uint32_t> {
public:
    //  Number of lines to write from base address before wrapping back
    //   to base address. 0 = no wrapping, >0 = last line written before
    //   wrapping
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, ping::DownScaled::Primary, 0x1c2a0)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, ping::DownScaled::Uv, 0x1c2f8)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, ping::FullResolution::Primary, 0x1c12c)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, ping::FullResolution::Uv, 0x1c184)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, pong::DownScaled::Primary, 0x34260)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, pong::DownScaled::Uv, 0x342b8)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, pong::FullResolution::Primary, 0x340ec)
DEF_NAMESPACE_REG(DmaWriter_LinesWrapped, pong::FullResolution::Uv, 0x34144)

class DmaWriter_LinetickFirst :
      public hwreg::RegisterBase<DmaWriter_LinetickFirst, uint32_t> {
public:
    //  Line number of first linetick. 0  = no linetick, >0 = line number
    //   to generate linetick
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, ping::DownScaled::Primary, 0x1c2a4)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, ping::DownScaled::Uv, 0x1c2fc)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, ping::FullResolution::Primary, 0x1c130)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, ping::FullResolution::Uv, 0x1c188)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, pong::DownScaled::Primary, 0x34264)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, pong::DownScaled::Uv, 0x342bc)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, pong::FullResolution::Primary, 0x340f0)
DEF_NAMESPACE_REG(DmaWriter_LinetickFirst, pong::FullResolution::Uv, 0x34148)

class DmaWriter_LinetickRepeat :
      public hwreg::RegisterBase<DmaWriter_LinetickRepeat, uint32_t> {
public:
    //  Line repeat interval of linetick. 0 = no repeat, >0 = repeat
    //   interval in lines
    DEF_FIELD(15, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, ping::DownScaled::Primary, 0x1c2a8)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, ping::DownScaled::Uv, 0x1c300)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, ping::FullResolution::Primary, 0x1c134)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, ping::FullResolution::Uv, 0x1c18c)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, pong::DownScaled::Primary, 0x34268)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, pong::DownScaled::Uv, 0x342c0)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, pong::FullResolution::Primary, 0x340f4)
DEF_NAMESPACE_REG(DmaWriter_LinetickRepeat, pong::FullResolution::Uv, 0x3414c)

class DmaWriter_LineTick : public hwreg::RegisterBase<DmaWriter_LineTick, uint32_t> {
public:
    //  Linetick delay in vcke cycles to add to min 3 cycle latency from
    //   acl_vi. 0-65535.
    //  Must be less than next linetick generation time or count will not
    //   mature and no linetick is not produced.
    //    --NOTE: linetick delay  can run past end of frame/field and
    //       also into next frame!
    //    --      Take care maturity time is less than next configured
    //             linetick generation postion!
    //   --      Take care when changing config between frame too!
    DEF_FIELD(31, 16, linetick_delay);
    //  Linetick start/end of line control. 0 = use start of line, 1 =
    //   use end of line to generate linetick
    DEF_BIT(0, linetick_eol);
};

DEF_NAMESPACE_REG(DmaWriter_LineTick, ping::DownScaled::Primary, 0x1c2ac)
DEF_NAMESPACE_REG(DmaWriter_LineTick, ping::DownScaled::Uv, 0x1c304)
DEF_NAMESPACE_REG(DmaWriter_LineTick, ping::FullResolution::Primary, 0x1c138)
DEF_NAMESPACE_REG(DmaWriter_LineTick, ping::FullResolution::Uv, 0x1c190)
DEF_NAMESPACE_REG(DmaWriter_LineTick, pong::DownScaled::Primary, 0x3426c)
DEF_NAMESPACE_REG(DmaWriter_LineTick, pong::DownScaled::Uv, 0x342c4)
DEF_NAMESPACE_REG(DmaWriter_LineTick, pong::FullResolution::Primary, 0x340f8)
DEF_NAMESPACE_REG(DmaWriter_LineTick, pong::FullResolution::Uv, 0x34150)

class DmaWriter_Axi : public hwreg::RegisterBase<DmaWriter_Axi, uint32_t> {
public:
    //  memory boundary that splits bursts:
    //   0=2Transfers,1=4Transfers,2=8Transfers,3=16Transfers. (for
    //   axi_data_w=128,  16transfers=256Bytes). Good default = 11
    DEF_FIELD(3, 2, axi_burstsplit);
    // value to send for awcache. Good default = 1111
    DEF_FIELD(11, 8, axi_cache_value);
    //  max outstanding write transactions (bursts) allowed. zero means
    //   no maximum(uses internal limit of 2048).
    DEF_FIELD(23, 16, axi_maxostand);
    //  max value to use for awlen (axi burst length). 0000= max 1
    //   transfer/burst , upto 1111= max 16 transfers/burst
    DEF_FIELD(27, 24, axi_max_awlen);
    //  active high, enables posting of pagewarm dummy writes to SMMU for
    //   early page translation of upcomming 4K pages.
    //   Recommend SMMU has min 8 page cache to avoid translation miss.
    //    Pagewarms are posted as dummy writes with wstrb= 0
    DEF_BIT(0, pagewarm_on);
    //  0= static value (axi_id_value) for awid/wid, 1 = incrementing
    //      value per transaction for awid/wid wrapping to 0 after
    //      axi_id_value
    DEF_BIT(1, axi_id_multi);
};

DEF_NAMESPACE_REG(DmaWriter_Axi, ping::DownScaled::Primary, 0x1c2b0)
DEF_NAMESPACE_REG(DmaWriter_Axi, ping::DownScaled::Uv, 0x1c308)
DEF_NAMESPACE_REG(DmaWriter_Axi, ping::FullResolution::Primary, 0x1c13c)
DEF_NAMESPACE_REG(DmaWriter_Axi, ping::FullResolution::Uv, 0x1c194)
DEF_NAMESPACE_REG(DmaWriter_Axi, pong::DownScaled::Primary, 0x34270)
DEF_NAMESPACE_REG(DmaWriter_Axi, pong::DownScaled::Uv, 0x342c8)
DEF_NAMESPACE_REG(DmaWriter_Axi, pong::FullResolution::Primary, 0x340fc)
DEF_NAMESPACE_REG(DmaWriter_Axi, pong::FullResolution::Uv, 0x34154)

class DmaWriter_AxiIdValue : public hwreg::RegisterBase<DmaWriter_AxiIdValue, uint32_t> {
public:
    // value to send for awid, wid and expected on bid. Good default = 0000
    DEF_FIELD(3, 0, value);
};

DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, ping::DownScaled::Primary, 0x1c2b4)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, ping::DownScaled::Uv, 0x1c30c)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, ping::FullResolution::Primary, 0x1c140)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, ping::FullResolution::Uv, 0x1c198)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, pong::DownScaled::Primary, 0x34274)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, pong::DownScaled::Uv, 0x342cc)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, pong::FullResolution::Primary, 0x34100)
DEF_NAMESPACE_REG(DmaWriter_AxiIdValue, pong::FullResolution::Uv, 0x34158)

class MultiCtx_ConfigDone : public hwreg::RegisterBase<MultiCtx_ConfigDone, uint32_t> {
public:
    // This signal is only required in multi-context mode
    //      Once configuration for ping/pong address space is done, MCU
    //       must write 1 into this address
    //     This register is self-clearing. So the read-back will be 0
    DEF_BIT(0, value);
};

DEF_NAMESPACE_REG(MultiCtx_ConfigDone, ping, 0x1c310)
DEF_NAMESPACE_REG(MultiCtx_ConfigDone, pong, 0x342d0)

} // namespace camera
