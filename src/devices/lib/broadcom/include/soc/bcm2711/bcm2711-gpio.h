// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_BROADCOM_INCLUDE_SOC_BCM2711_BCM2711_GPIO_H_
#define SRC_DEVICES_LIB_BROADCOM_INCLUDE_SOC_BCM2711_BCM2711_GPIO_H_

// General register params
#define BCM2711_GPIO_REG_SIZE 32        // 32 pins per regs
#define BCM2711_GPIO_INT_REG_NUM 2      // number of interrupt event and config regs

// GPIO Function Select
#define BCM2711_GPIO_FSEL0 0x00

#define BCM2711_GPIO_FSEL_INPUT 0b000
#define BCM2711_GPIO_FSEL_OUTPUT 0b001
#define BCM2711_GPIO_FSEL_ALTFUN0 0b100
#define BCM2711_GPIO_FSEL_ALTFUN1 0b101
#define BCM2711_GPIO_FSEL_ALTFUN2 0b110
#define BCM2711_GPIO_FSEL_ALTFUN3 0b111
#define BCM2711_GPIO_FSEL_ALTFUN4 0b011
#define BCM2711_GPIO_FSEL_ALTFUN5 0b010
#define BCM2711_GPIO_FSEL_ALT_NUM 6

#define BCM2711_GPIO_FSEL_MASK(x) (0b111 << ((3 * x) % 30)) // 10 pins per regs

// general mask and shift macros for pin operations
#define BCM2711_GPIO_PIN(x) x
#define BCM2711_GPIO_MASK(x) (1u << (x % BCM2711_GPIO_REG_SIZE)) 

// GPIO Pin Output Set
#define BCM2711_GPIO_SET0 0x1c

// GPIO Pin Output Clear
#define BCM2711_GPIO_CLR0 0x28

// GPIO Pin Level
#define BCM2711_GPIO_LEV0 0x34

// GPIO Pin Event Detect Status
#define BCM2711_GPIO_EDS0 0x40

// GPIO Pin Rising Edge Detect Enable
#define BCM2711_GPIO_REN0 0x4c

// GPIO Pin Falling Edge Detect Enable
#define BCM2711_GPIO_FEN0 0x58

// GPIO Pin High Detect Enable
#define BCM2711_GPIO_HEN0 0x64

// GPIO Pin Low Detect Enable
#define BCM2711_GPIO_LEN0 0x70

// GPIO Pin Async. Rising Edge Detect
#define BCM2711_GPIO_AREN0 0x7c

#define BCM2711_GPIO_ASYNC_RISING_EDGE_DETECT_DISABLED 0
#define BCM2711_GPIO_ASYNC_RISING_EDGE_DETECT_ENABLED 1

// GPIO Pin Async. Falling Edge Detect
#define BCM2711_GPIO_AFEN0 0x88

#define BCM2711_GPIO_ASYNC_FALLING_EDGE_DETECT_DISABLED 0
#define BCM2711_GPIO_ASYNC_FALLING_EDGE_DETECT_ENABLED 1

// GPIO Pull-up / Pull-down
#define BCM2711_GPIO_PUP_PDN_CNTRL_REG0 0xe4

#define BCM2711_GPIO_NO_RESISTOR 0b00
#define BCM2711_GPIO_PULL_UP 0b01
#define BCM2711_GPIO_PULL_DOWN 0b10

// Alternative Function Assignments
#define BCM2711_GPIO_00_SDA0                0
#define BCM2711_GPIO_00_SA5                 1
#define BCM2711_GPIO_00_PCLK                2
#define BCM2711_GPIO_00_SPI3_CE0_N          3
#define BCM2711_GPIO_00_TXD2                4
#define BCM2711_GPIO_00_SDA6                5

#define BCM2711_GPIO_01_SCL0                6
#define BCM2711_GPIO_01_SA4                 7
#define BCM2711_GPIO_01_DE                  8
#define BCM2711_GPIO_01_SPI3_MISO           9
#define BCM2711_GPIO_01_RXD2               10
#define BCM2711_GPIO_01_SCL6               11

#define BCM2711_GPIO_02_SDA1               12
#define BCM2711_GPIO_02_SA3                13
#define BCM2711_GPIO_02_LCD_VSYNC          14
#define BCM2711_GPIO_02_SPI3_MOSI          15
#define BCM2711_GPIO_02_CTS2               16
#define BCM2711_GPIO_02_SDA3               17

#define BCM2711_GPIO_03_SCL1               18
#define BCM2711_GPIO_03_SA2                19
#define BCM2711_GPIO_03_LCD_HSYNC          20
#define BCM2711_GPIO_03_SPI3_SCLK          21
#define BCM2711_GPIO_03_RTS2               22
#define BCM2711_GPIO_03_SCL3               23

#define BCM2711_GPIO_04_GPCLK0             24
#define BCM2711_GPIO_04_SA1                25
#define BCM2711_GPIO_04_DPI_D0             26
#define BCM2711_GPIO_04_SPI4_CE0_N         27
#define BCM2711_GPIO_04_TXD3               28
#define BCM2711_GPIO_04_SDA3               29

#define BCM2711_GPIO_05_GPCLK1             30
#define BCM2711_GPIO_05_SA0                31
#define BCM2711_GPIO_05_DPI_D1             32
#define BCM2711_GPIO_05_SPI4_MISO          33
#define BCM2711_GPIO_05_RXD3               34
#define BCM2711_GPIO_05_SCL3               35

#define BCM2711_GPIO_06_GPCLK2             36
#define BCM2711_GPIO_06_SOE_N_SE           37
#define BCM2711_GPIO_06_DPI_D2             38
#define BCM2711_GPIO_06_SPI4_MOSI          39
#define BCM2711_GPIO_06_CTS3               40
#define BCM2711_GPIO_06_SDA4               41

#define BCM2711_GPIO_07_SPI0_CE1_N         42
#define BCM2711_GPIO_07_SWE_N_SRW_N        43
#define BCM2711_GPIO_07_DPI_D3             44
#define BCM2711_GPIO_07_SPI4_SCLK          45
#define BCM2711_GPIO_07_RTS3               46
#define BCM2711_GPIO_07_SCL4               47

#define BCM2711_GPIO_08_SPI0_CE0_N         48
#define BCM2711_GPIO_08_SD0                49
#define BCM2711_GPIO_08_DPI_D4             50
#define BCM2711_GPIO_08_BSCSL_CE_N         51
#define BCM2711_GPIO_08_TXD4               52
#define BCM2711_GPIO_08_SDA4               53

#define BCM2711_GPIO_09_SPI0_MISO          54
#define BCM2711_GPIO_09_SD1                55
#define BCM2711_GPIO_09_DPI_D5             56
#define BCM2711_GPIO_09_BSCSL_MISO         57
#define BCM2711_GPIO_09_RXD4               58
#define BCM2711_GPIO_09_SCL4               59

#define BCM2711_GPIO_10_SPI0_MOSI          60
#define BCM2711_GPIO_10_SD2                61
#define BCM2711_GPIO_10_DPI_D6             62
#define BCM2711_GPIO_10_BSCSL_SDA_MOSI     63
#define BCM2711_GPIO_10_CTS4               64
#define BCM2711_GPIO_10_SDA5               65

#define BCM2711_GPIO_11_SPI0_CLK           66
#define BCM2711_GPIO_11_SD3                67
#define BCM2711_GPIO_11_DPI_D7             68
#define BCM2711_GPIO_11_BSCSL_SCL_SCLK     69
#define BCM2711_GPIO_11_RTS4               70
#define BCM2711_GPIO_11_SCL5               71

#define BCM2711_GPIO_12_PWM0_0             72
#define BCM2711_GPIO_12_SD4                73
#define BCM2711_GPIO_12_DPI_D8             74
#define BCM2711_GPIO_12_SPI5_CE0_N         75
#define BCM2711_GPIO_12_TXD5               76
#define BCM2711_GPIO_12_SDA5               77

#define BCM2711_GPIO_13_PWM0_1             78
#define BCM2711_GPIO_13_SD5                79
#define BCM2711_GPIO_13_DPI_D9             80
#define BCM2711_GPIO_13_SPI5_MISO          81
#define BCM2711_GPIO_13_RXD5               82
#define BCM2711_GPIO_13_SCL5               83

#define BCM2711_GPIO_14_TXD0               84
#define BCM2711_GPIO_14_SD6                85
#define BCM2711_GPIO_14_DPI_D10            86
#define BCM2711_GPIO_14_SPI5_MOSI          87
#define BCM2711_GPIO_14_CTS5               88
#define BCM2711_GPIO_14_TXD1               89

#define BCM2711_GPIO_15_RXD0               90
#define BCM2711_GPIO_15_SD7                91
#define BCM2711_GPIO_15_DPI_D11            92
#define BCM2711_GPIO_15_SPI5_SCLK          93
#define BCM2711_GPIO_15_RTS5               94
#define BCM2711_GPIO_15_RXD1               95

                                           //
#define BCM2711_GPIO_16_SD8                97
#define BCM2711_GPIO_16_DPI_D12            98
#define BCM2711_GPIO_16_CTS0               99
#define BCM2711_GPIO_16_SPI1_CE2_N        100
#define BCM2711_GPIO_16_CTS1              101

                                           //
#define BCM2711_GPIO_17_SD9               103
#define BCM2711_GPIO_17_DPI_D13           104
#define BCM2711_GPIO_17_RTS0              105
#define BCM2711_GPIO_17_SPI1_CE1_N        106
#define BCM2711_GPIO_17_RTS1              107

#define BCM2711_GPIO_18_PCM_CLK           108
#define BCM2711_GPIO_18_SD10              109
#define BCM2711_GPIO_18_DPI_D14           110
#define BCM2711_GPIO_18_SPI6_CE0_N        111
#define BCM2711_GPIO_18_SPI1_CE0_N        112
#define BCM2711_GPIO_18_PWM0_0            113

#define BCM2711_GPIO_19_PCM_FS            114
#define BCM2711_GPIO_19_SD11              115
#define BCM2711_GPIO_19_DPI_D15           116
#define BCM2711_GPIO_19_SPI6_MISO         117
#define BCM2711_GPIO_19_SPI1_MISO         118
#define BCM2711_GPIO_19_PWM0_1            119

#define BCM2711_GPIO_20_PCM_DIN           120
#define BCM2711_GPIO_20_SD12              121
#define BCM2711_GPIO_20_DPI_D16           122
#define BCM2711_GPIO_20_SPI6_MOSI         123
#define BCM2711_GPIO_20_SPI1_MOSI         124
#define BCM2711_GPIO_20_GPCLK0            125

#define BCM2711_GPIO_21_PCM_DOUT          126
#define BCM2711_GPIO_21_SD13              127
#define BCM2711_GPIO_21_DPI_D17           128
#define BCM2711_GPIO_21_SPI6_SCLK         129
#define BCM2711_GPIO_21_SPI1_SCLK         130
#define BCM2711_GPIO_21_GPCLK1            131

#define BCM2711_GPIO_22_SD0_CLK           132
#define BCM2711_GPIO_22_SD14              133
#define BCM2711_GPIO_22_DPI_D18           134
#define BCM2711_GPIO_22_SD1_CLK           135
#define BCM2711_GPIO_22_ARM_TRST          136
#define BCM2711_GPIO_22_SDA6              137

#define BCM2711_GPIO_23_SD0_CMD           138
#define BCM2711_GPIO_23_SD15              139
#define BCM2711_GPIO_23_DPI_D19           140
#define BCM2711_GPIO_23_SD1_CMD           141
#define BCM2711_GPIO_23_ARM_RTCK          142
#define BCM2711_GPIO_23_SCL6              143

#define BCM2711_GPIO_24_SD0_DAT0          144
#define BCM2711_GPIO_24_SD16              145
#define BCM2711_GPIO_24_DPI_D20           146
#define BCM2711_GPIO_24_SD1_DAT0          147
#define BCM2711_GPIO_24_ARM_TDO           148
#define BCM2711_GPIO_24_SPI3_CE1_N        149

#define BCM2711_GPIO_25_SD0_DAT1          150
#define BCM2711_GPIO_25_SD17              151
#define BCM2711_GPIO_25_DPI_D21           152
#define BCM2711_GPIO_25_SD1_DAT1          153
#define BCM2711_GPIO_25_ARM_TCK           154
#define BCM2711_GPIO_25_SPI4_CE1_N        155

#define BCM2711_GPIO_26_SD0_DAT2          156
                                           //
#define BCM2711_GPIO_26_DPI_D22           158
#define BCM2711_GPIO_26_SD1_DAT2          159
#define BCM2711_GPIO_26_ARM_TDI           160
#define BCM2711_GPIO_26_SPI5_CE1_N        161

#define BCM2711_GPIO_27_SD0_DAT3          162
                                           //
#define BCM2711_GPIO_27_DPI_D23           164
#define BCM2711_GPIO_27_SD1_DAT3          165
#define BCM2711_GPIO_27_ARM_TMS           166
#define BCM2711_GPIO_27_SPI6_CE1_N        167

#define BCM2711_GPIO_28_SDA0              168
#define BCM2711_GPIO_28_SA5               169
#define BCM2711_GPIO_28_PCM_CLK           170
                                           //
#define BCM2711_GPIO_28_MII_A_RX_ERR      172
#define BCM2711_GPIO_28_RGMII_MDIO        173

#define BCM2711_GPIO_29_SCL0              174
#define BCM2711_GPIO_29_SA4               175
#define BCM2711_GPIO_29_PCM_FS            176
                                           //
#define BCM2711_GPIO_29_MII_A_TX_ERR      178
#define BCM2711_GPIO_29_RGMII_MDC         179

                                           //
#define BCM2711_GPIO_30_SA3               181
#define BCM2711_GPIO_30_PCM_DIN           182
#define BCM2711_GPIO_30_CTS0              183
#define BCM2711_GPIO_30_MII_A_CRS         184
#define BCM2711_GPIO_30_CTS1              185

                                           //
#define BCM2711_GPIO_31_SA2               187
#define BCM2711_GPIO_31_PCM_DOUT          188
#define BCM2711_GPIO_31_RTS0              189
#define BCM2711_GPIO_31_MII_A_COL         190
#define BCM2711_GPIO_31_RTS1              191

#define BCM2711_GPIO_32_GPCLK0            192
#define BCM2711_GPIO_32_SA1               193
                                           //
#define BCM2711_GPIO_32_TXD0              195
#define BCM2711_GPIO_32_SD_CARD_PRES      196
#define BCM2711_GPIO_32_TXD1              197

                                           //
#define BCM2711_GPIO_33_SA0               199
                                           //
#define BCM2711_GPIO_33_RXD0              201
#define BCM2711_GPIO_33_SD_CARD_WRPROT    202
#define BCM2711_GPIO_33_RXD1              203

#define BCM2711_GPIO_34_GPCLK0            204    
#define BCM2711_GPIO_34_SOE_N_SE          205
                                           //
#define BCM2711_GPIO_34_SD1_CLK           207
#define BCM2711_GPIO_34_SD_CARD_LED       208
#define BCM2711_GPIO_34_RGMII_IRQ         209

#define BCM2711_GPIO_35_SPI0_CE1_N        210
#define BCM2711_GPIO_35_SWE_N_SRW_N       211
                                           //
#define BCM2711_GPIO_35_SD1_CMD           213
#define BCM2711_GPIO_35_RGMII_START_STOP  214
                                           //

#define BCM2711_GPIO_36_SPI0_CE0_N        216
#define BCM2711_GPIO_36_SD0               217
#define BCM2711_GPIO_36_TXD0              218
#define BCM2711_GPIO_36_SD1_DAT0          219
#define BCM2711_GPIO_36_RGMII_RX_OK       220
#define BCM2711_GPIO_36_MII_A_RX_ERR      221

#define BCM2711_GPIO_37_SPI0_MISO         222
#define BCM2711_GPIO_37_SD1               223
#define BCM2711_GPIO_37_RXD0              224
#define BCM2711_GPIO_37_SD1_DAT1          225
#define BCM2711_GPIO_37_RGMII_MDIO        226
#define BCM2711_GPIO_37_MII_A_TX_ERR      227

#define BCM2711_GPIO_38_SPI0_MOSI         228
#define BCM2711_GPIO_38_SD2               229
#define BCM2711_GPIO_38_RTS0              230
#define BCM2711_GPIO_38_SD1_DAT2          231
#define BCM2711_GPIO_38_RGMII_MDC         232
#define BCM2711_GPIO_38_MII_A_CRS         233

#define BCM2711_GPIO_39_SPI0_CLK          234
#define BCM2711_GPIO_39_SD3               235
#define BCM2711_GPIO_39_CTS0              236
#define BCM2711_GPIO_39_SD1_DAT3          237
#define BCM2711_GPIO_39_RGMII_IRQ         238
#define BCM2711_GPIO_39_MII_A_COL         239

#define BCM2711_GPIO_40_PWM1_0            240
#define BCM2711_GPIO_40_SD4               241
                                           //
#define BCM2711_GPIO_40_SD1_DAT4          243
#define BCM2711_GPIO_40_SPI0_MISO         244
#define BCM2711_GPIO_40_TXD1              245

#define BCM2711_GPIO_41_PWM1_1            246
#define BCM2711_GPIO_41_SD5               247
                                           //
#define BCM2711_GPIO_41_SD1_DAT5          249
#define BCM2711_GPIO_41_SPI0_MOSI         250
#define BCM2711_GPIO_41_RXD1              251

#define BCM2711_GPIO_42_GPCLK1            252
#define BCM2711_GPIO_42_SD6               253
                                           //
#define BCM2711_GPIO_42_SD1_DAT6          255
#define BCM2711_GPIO_42_SPI0_SCLK         256
#define BCM2711_GPIO_42_RTS1              257

#define BCM2711_GPIO_43_GPCLK2            258
#define BCM2711_GPIO_43_SD7               259
                                           //
#define BCM2711_GPIO_43_SD1_DAT7          261
#define BCM2711_GPIO_43_SPI0_CE0_N        262
#define BCM2711_GPIO_43_CTS1              263

#define BCM2711_GPIO_44_GPCLK1            264
#define BCM2711_GPIO_44_SDA0              265
#define BCM2711_GPIO_44_SDA1              266
                                           //
#define BCM2711_GPIO_44_SPI0_CE1_N        268
#define BCM2711_GPIO_44_SD_CARD_VOLT      269

#define BCM2711_GPIO_45_PWM0_1            270
#define BCM2711_GPIO_45_SCL0              271
#define BCM2711_GPIO_45_SCL1              272
                                           //
#define BCM2711_GPIO_45_SPI0_CE2_N        274
#define BCM2711_GPIO_45_SD_CARD_PWR0      275

#endif  // SRC_DEVICES_LIB_BROADCOM_INCLUDE_SOC_BCM2711_BCM2711_GPIO_H_
