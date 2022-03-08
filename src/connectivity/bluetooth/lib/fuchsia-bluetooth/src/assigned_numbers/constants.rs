// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::AssignedNumber;
use crate::assigned_number;

pub(super) const SERVICE_UUIDS: [AssignedNumber; 40] = [
    assigned_number!(0x1800, "GAP", "Generic Access"),
    assigned_number!(0x1801, "GATT", "Generic Attribute"),
    assigned_number!(0x1802, "IAS", "Immediate Alert Service"),
    assigned_number!(0x1803, "LLS", "Link Loss Service"),
    assigned_number!(0x1804, "TPS", "Tx Power Service"),
    assigned_number!(0x1805, "CTS", "Current Time Service"),
    assigned_number!(0x1806, "RTUS", "Reference Time Update Service"),
    assigned_number!(0x1807, "NDCS", "Next DST Change Service"),
    assigned_number!(0x1808, "GLS", "Glucose Service"),
    assigned_number!(0x1809, "HTS", "Health Thermometer Service"),
    assigned_number!(0x180A, "DIS", "Device Information Service"),
    assigned_number!(0x180D, "HRS", "Heart Rate Service"),
    assigned_number!(0x180E, "PASS", "Phone Alert Status Service"),
    assigned_number!(0x180F, "BAS", "Battery Service"),
    assigned_number!(0x1810, "BLS", "Blood Pressure Service"),
    assigned_number!(0x1811, "ANS", "Alert Notification Service"),
    assigned_number!(0x1812, "HIDS", "Human Interface Device Service"),
    assigned_number!(0x1813, "SCPS", "Scan Parameters Service"),
    assigned_number!(0x1814, "RSCS", "Running Speed and Cadence Service"),
    assigned_number!(0x1815, "AIOS", "Automation IO Service"),
    assigned_number!(0x1816, "CSCS", "Cycling Speed and Cadence Service"),
    assigned_number!(0x1818, "CPS", "Cycling Power Service"),
    assigned_number!(0x1819, "LNS", "Location and Navigation Service"),
    assigned_number!(0x181A, "ESS", "Environmental Sensing Service"),
    assigned_number!(0x181B, "BCS", "Body Composition Service"),
    assigned_number!(0x181C, "UDS", "User Data Service"),
    assigned_number!(0x181D, "WSS", "Weight Scale Service"),
    assigned_number!(0x181E, "BMS", "Bond Management Service"),
    assigned_number!(0x181F, "CGMS", "Continuous Glucose Monitoring Service"),
    assigned_number!(0x1820, "IPSS", "Internet Protocol Support Service"),
    assigned_number!(0x1821, "IPS", "Indoor Positioning Service"),
    assigned_number!(0x1822, "PLXS", "Pulse Oximeter Service"),
    assigned_number!(0x1823, "HPS", "Http Proxy Service"),
    assigned_number!(0x1824, "TDS", "Transport Discovery Service"),
    assigned_number!(0x1825, "OTS", "Object Transfer Service"),
    assigned_number!(0x1826, "FTMS", "Fitness Machine Service"),
    assigned_number!(0x1827, "MPVS", "Mesh Provisioning Service"),
    assigned_number!(0x1828, "MPS", "Mesh Proxy Service"),
    assigned_number!(0x1829, "RCS", "Reconnection Configuration Service"),
    assigned_number!(0x183A, "IDS", "Insulin Delivery Service"),
];

/// Custom service uuids are SIG allocated 16-bit Universally Unique Identifier (UUID)
/// for use with a custom GATT-based service defined and registered by members.
/// Member names are used here in the `name` field of the `AssignedNumber`.
///
/// Source: https://www.bluetooth.com/specifications/assigned-numbers/16-bit-uuids-for-members
pub(super) const CUSTOM_SERVICE_UUIDS: [AssignedNumber; 304] = [
    assigned_number!(0xFDCF, "Nalu Medical, Inc"),
    assigned_number!(0xFDD0, "Huawei Technologies Co., Ltd"),
    assigned_number!(0xFDD1, "Huawei Technologies Co., Ltd"),
    assigned_number!(0xFDD2, "Bose Corporation"),
    assigned_number!(0xFDD3, "FUBA Automotive Electronics GmbH"),
    assigned_number!(0xFDD4, "LX Solutions Pty Limited"),
    assigned_number!(0xFDD5, "Brompton Bicycle Ltd"),
    assigned_number!(0xFDD6, "Ministry of Supply"),
    assigned_number!(0xFDD7, "Emerson"),
    assigned_number!(0xFDD8, "Jiangsu Teranovo Tech Co., Ltd."),
    assigned_number!(0xFDD9, "Jiangsu Teranovo Tech Co., Ltd."),
    assigned_number!(0xFDDA, "MHCS"),
    assigned_number!(0xFDDB, "Samsung Electronics Co., Ltd."),
    assigned_number!(0xFDDC, "4iiii Innovations Inc."),
    assigned_number!(0xFDDD, "Arch Systems Inc"),
    assigned_number!(0xFDDE, "Noodle Technology Inc."),
    assigned_number!(0xFDDF, "Harman International"),
    assigned_number!(0xFDE0, "John Deere"),
    assigned_number!(0xFDE1, "Fortin Electronic Systems"),
    assigned_number!(0xFDE2, "Google Inc."),
    assigned_number!(0xFDE3, "Abbott Diabetes Care"),
    assigned_number!(0xFDE4, "JUUL Labs, Inc."),
    assigned_number!(0xFDE5, "SMK Corporation"),
    assigned_number!(0xFDE6, "Intelletto Technologies Inc"),
    assigned_number!(0xFDE7, "SECOM Co., LTD"),
    assigned_number!(0xFDE8, "Robert Bosch GmbH"),
    assigned_number!(0xFDE9, "Spacesaver Corporation"),
    assigned_number!(0xFDEA, "SeeScan, Inc"),
    assigned_number!(0xFDEB, "Syntronix Corporation"),
    assigned_number!(0xFDEC, "Mannkind Corporation"),
    assigned_number!(0xFDED, "Pole Star"),
    assigned_number!(0xFDEE, "Huawei Technologies Co., Ltd."),
    assigned_number!(0xFDEF, "ART AND PROGRAM, INC."),
    assigned_number!(0xFDF0, "Google Inc."),
    assigned_number!(0xFDF1, "LAMPLIGHT Co.,Ltd"),
    assigned_number!(0xFDF2, "AMICCOM Electronics Corporation"),
    assigned_number!(0xFDF3, "Amersports"),
    assigned_number!(0xFDF4, "O. E. M. Controls, Inc."),
    assigned_number!(0xFDF5, "Milwaukee Electric Tools"),
    assigned_number!(0xFDF6, "AIAIAI ApS"),
    assigned_number!(0xFDF7, "HP Inc."),
    assigned_number!(0xFDF8, "Onvocal"),
    assigned_number!(0xFDF9, "INIA"),
    assigned_number!(0xFDFA, "Tandem Diabetes Care"),
    assigned_number!(0xFDFB, "Tandem Diabetes Care"),
    assigned_number!(0xFDFC, "Optrel AG"),
    assigned_number!(0xFDFD, "RecursiveSoft Inc."),
    assigned_number!(0xFDFE, "ADHERIUM(NZ) LIMITED"),
    assigned_number!(0xFDFF, "OSRAM GmbH"),
    assigned_number!(0xFE00, "Amazon.com Services, Inc."),
    assigned_number!(0xFE01, "Duracell U.S. Operations Inc."),
    assigned_number!(0xFE02, "Robert Bosch GmbH"),
    assigned_number!(0xFE03, "Amazon.com Services, Inc."),
    assigned_number!(0xFE04, "OpenPath Security Inc"),
    assigned_number!(0xFE05, "CORE Transport Technologies NZ Limited"),
    assigned_number!(0xFE06, "Qualcomm Technologies, Inc."),
    assigned_number!(0xFE08, "Microsoft"),
    assigned_number!(0xFE09, "Pillsy, Inc."),
    assigned_number!(0xFE0A, "ruwido austria gmbh"),
    assigned_number!(0xFE0B, "ruwido austria gmbh"),
    assigned_number!(0xFE0C, "Procter & Gamble"),
    assigned_number!(0xFE0D, "Procter & Gamble"),
    assigned_number!(0xFE0E, "Setec Pty Ltd"),
    assigned_number!(0xFE0F, "Philips Lighting B.V."),
    assigned_number!(0xFE10, "Lapis Semiconductor Co., Ltd."),
    assigned_number!(0xFE11, "GMC-I Messtechnik GmbH"),
    assigned_number!(0xFE12, "M-Way Solutions GmbH"),
    assigned_number!(0xFE13, "Apple Inc."),
    assigned_number!(0xFE14, "Flextronics International USA Inc."),
    assigned_number!(0xFE15, "Amazon.com Services, Inc."),
    assigned_number!(0xFE16, "Footmarks, Inc."),
    assigned_number!(0xFE17, "Telit Wireless Solutions GmbH"),
    assigned_number!(0xFE18, "Runtime, Inc."),
    assigned_number!(0xFE19, "Google Inc."),
    assigned_number!(0xFE1A, "Tyto Life LLC"),
    assigned_number!(0xFE1B, "Tyto Life LLC"),
    assigned_number!(0xFE1C, "NetMedia, Inc."),
    assigned_number!(0xFE1D, "Illuminati Instrument Corporation"),
    assigned_number!(0xFE1E, "Smart Innovations Co., Ltd"),
    assigned_number!(0xFE1F, "Garmin International, Inc."),
    assigned_number!(0xFE20, "Emerson"),
    assigned_number!(0xFE21, "Bose Corporation"),
    assigned_number!(0xFE22, "Zoll Medical Corporation"),
    assigned_number!(0xFE23, "Zoll Medical Corporation"),
    assigned_number!(0xFE24, "August Home Inc"),
    assigned_number!(0xFE25, "Apple, Inc."),
    assigned_number!(0xFE26, "Google Inc."),
    assigned_number!(0xFE27, "Google Inc."),
    assigned_number!(0xFE28, "Ayla Networks"),
    assigned_number!(0xFE29, "Gibson Innovations"),
    assigned_number!(0xFE2A, "DaisyWorks, Inc."),
    assigned_number!(0xFE2B, "ITT Industries"),
    assigned_number!(0xFE2C, "Google Inc."),
    assigned_number!(0xFE2D, "SMART INNOVATION Co.,Ltd"),
    assigned_number!(0xFE2E, "ERi,Inc."),
    assigned_number!(0xFE2F, "CRESCO Wireless, Inc"),
    assigned_number!(0xFE30, "Volkswagen AG"),
    assigned_number!(0xFE31, "Volkswagen AG"),
    assigned_number!(0xFE32, "Pro-Mark, Inc."),
    assigned_number!(0xFE33, "CHIPOLO d.o.o."),
    assigned_number!(0xFE34, "SmallLoop LLC"),
    assigned_number!(0xFE35, "HUAWEI Technologies Co., Ltd"),
    assigned_number!(0xFE36, "HUAWEI Technologies Co., Ltd"),
    assigned_number!(0xFE37, "Spaceek LTD"),
    assigned_number!(0xFE38, "Spaceek LTD"),
    assigned_number!(0xFE39, "TTS Tooltechnic Systems AG & Co. KG"),
    assigned_number!(0xFE3A, "TTS Tooltechnic Systems AG & Co. KG"),
    assigned_number!(0xFE3B, "Dolby Laboratories"),
    assigned_number!(0xFE3C, "Alibaba"),
    assigned_number!(0xFE3D, "BD Medical"),
    assigned_number!(0xFE3E, "BD Medical"),
    assigned_number!(0xFE3F, "Friday Labs Limited"),
    assigned_number!(0xFE40, "Inugo Systems Limited"),
    assigned_number!(0xFE41, "Inugo Systems Limited"),
    assigned_number!(0xFE42, "Nets A/S"),
    assigned_number!(0xFE43, "Andreas Stihl AG & Co. KG"),
    assigned_number!(0xFE44, "SK Telecom"),
    assigned_number!(0xFE45, "Snapchat Inc"),
    assigned_number!(0xFE46, "B&O Play A/S"),
    assigned_number!(0xFE47, "General Motors"),
    assigned_number!(0xFE48, "General Motors"),
    assigned_number!(0xFE49, "SenionLab AB"),
    assigned_number!(0xFE4A, "OMRON HEALTHCARE Co., Ltd."),
    assigned_number!(0xFE4B, "Philips Lighting B.V."),
    assigned_number!(0xFE4C, "Volkswagen AG"),
    assigned_number!(0xFE4D, "Casambi Technologies Oy"),
    assigned_number!(0xFE4E, "NTT docomo"),
    assigned_number!(0xFE4F, "Molekule, Inc."),
    assigned_number!(0xFE50, "Google Inc."),
    assigned_number!(0xFE51, "SRAM"),
    assigned_number!(0xFE52, "SetPoint Medical"),
    assigned_number!(0xFE53, "3M"),
    assigned_number!(0xFE54, "Motiv, Inc."),
    assigned_number!(0xFE55, "Google Inc."),
    assigned_number!(0xFE56, "Google Inc."),
    assigned_number!(0xFE57, "Dotted Labs"),
    assigned_number!(0xFE58, "Nordic Semiconductor ASA"),
    assigned_number!(0xFE59, "Nordic Semiconductor ASA"),
    assigned_number!(0xFE5A, "Chronologics Corporation"),
    assigned_number!(0xFE5B, "GT-tronics HK Ltd"),
    assigned_number!(0xFE5C, "million hunters GmbH"),
    assigned_number!(0xFE5D, "Grundfos A/S"),
    assigned_number!(0xFE5E, "Plastc Corporation"),
    assigned_number!(0xFE5F, "Eyefi, Inc."),
    assigned_number!(0xFE60, "Lierda Science & Technology Group Co., Ltd."),
    assigned_number!(0xFE61, "Logitech International SA"),
    assigned_number!(0xFE62, "Indagem Tech LLC"),
    assigned_number!(0xFE63, "Connected Yard, Inc."),
    assigned_number!(0xFE64, "Siemens AG"),
    assigned_number!(0xFE65, "CHIPOLO d.o.o."),
    assigned_number!(0xFE66, "Intel Corporation"),
    assigned_number!(0xFE67, "Lab Sensor Solutions"),
    assigned_number!(0xFE68, "Qualcomm Life Inc"),
    assigned_number!(0xFE69, "Qualcomm Life Inc"),
    assigned_number!(0xFE6A, "Kontakt Micro-Location Sp. z o.o."),
    assigned_number!(0xFE6B, "TASER International, Inc."),
    assigned_number!(0xFE6C, "TASER International, Inc."),
    assigned_number!(0xFE6D, "The University of Tokyo"),
    assigned_number!(0xFE6E, "The University of Tokyo"),
    assigned_number!(0xFE6F, "LINE Corporation"),
    assigned_number!(0xFE70, "Beijing Jingdong Century Trading Co., Ltd."),
    assigned_number!(0xFE71, "Plume Design Inc"),
    assigned_number!(0xFE72, "St. Jude Medical, Inc."),
    assigned_number!(0xFE73, "St. Jude Medical, Inc."),
    assigned_number!(0xFE74, "unwire"),
    assigned_number!(0xFE75, "TangoMe"),
    assigned_number!(0xFE76, "TangoMe"),
    assigned_number!(0xFE77, "Hewlett-Packard Company"),
    assigned_number!(0xFE78, "Hewlett-Packard Company"),
    assigned_number!(0xFE79, "Zebra Technologies"),
    assigned_number!(0xFE7A, "Bragi GmbH"),
    assigned_number!(0xFE7B, "Orion Labs, Inc."),
    assigned_number!(0xFE7C, "Telit Wireless Solutions (Formerly Stollmann E+V GmbH)"),
    assigned_number!(0xFE7D, "Aterica Health Inc."),
    assigned_number!(0xFE7E, "Awear Solutions Ltd"),
    assigned_number!(0xFE7F, "Doppler Lab"),
    assigned_number!(0xFE80, "Doppler Lab"),
    assigned_number!(0xFE81, "Medtronic Inc."),
    assigned_number!(0xFE82, "Medtronic Inc."),
    assigned_number!(0xFE83, "Blue Bite"),
    assigned_number!(0xFE84, "RF Digital Corp"),
    assigned_number!(0xFE85, "RF Digital Corp"),
    assigned_number!(0xFE86, "HUAWEI Technologies Co., Ltd. ( )"),
    assigned_number!(0xFE87, "Qingdao Yeelink Information Technology Co., Ltd. ( )"),
    assigned_number!(0xFE88, "SALTO SYSTEMS S.L."),
    assigned_number!(0xFE89, "B&O Play A/S"),
    assigned_number!(0xFE8A, "Apple, Inc."),
    assigned_number!(0xFE8B, "Apple, Inc."),
    assigned_number!(0xFE8C, "TRON Forum"),
    assigned_number!(0xFE8D, "Interaxon Inc."),
    assigned_number!(0xFE8E, "ARM Ltd"),
    assigned_number!(0xFE8F, "CSR"),
    assigned_number!(0xFE90, "JUMA"),
    assigned_number!(0xFE91, "Shanghai Imilab Technology Co.,Ltd"),
    assigned_number!(0xFE92, "Jarden Safety & Security"),
    assigned_number!(0xFE93, "OttoQ Inc."),
    assigned_number!(0xFE94, "OttoQ Inc."),
    assigned_number!(0xFE95, "Xiaomi Inc."),
    assigned_number!(0xFE96, "Tesla Motor Inc."),
    assigned_number!(0xFE97, "Tesla Motor Inc."),
    assigned_number!(0xFE98, "Currant, Inc."),
    assigned_number!(0xFE99, "Currant, Inc."),
    assigned_number!(0xFE9A, "Estimote"),
    assigned_number!(0xFE9B, "Samsara Networks, Inc"),
    assigned_number!(0xFE9C, "GSI Laboratories, Inc."),
    assigned_number!(0xFE9D, "Mobiquity Networks Inc"),
    assigned_number!(0xFE9E, "Dialog Semiconductor B.V."),
    assigned_number!(0xFE9F, "Google Inc."),
    assigned_number!(0xFEA0, "Google Inc."),
    assigned_number!(0xFEA1, "Intrepid Control Systems, Inc."),
    assigned_number!(0xFEA2, "Intrepid Control Systems, Inc."),
    assigned_number!(0xFEA3, "ITT Industries"),
    assigned_number!(0xFEA4, "Paxton Access Ltd"),
    assigned_number!(0xFEA5, "GoPro, Inc."),
    assigned_number!(0xFEA6, "GoPro, Inc."),
    assigned_number!(0xFEA7, "UTC Fire and Security"),
    assigned_number!(0xFEA8, "Savant Systems LLC"),
    assigned_number!(0xFEA9, "Savant Systems LLC"),
    assigned_number!(0xFEAA, "Google Inc."),
    assigned_number!(0xFEAB, "Nokia Corporation"),
    assigned_number!(0xFEAC, "Nokia Corporation"),
    assigned_number!(0xFEAD, "Nokia Corporation"),
    assigned_number!(0xFEAE, "Nokia Corporation"),
    assigned_number!(0xFEAF, "Nest Labs Inc."),
    assigned_number!(0xFEB0, "Nest Labs Inc."),
    assigned_number!(0xFEB1, "Electronics Tomorrow Limited"),
    assigned_number!(0xFEB2, "Microsoft Corporation"),
    assigned_number!(0xFEB3, "Taobao"),
    assigned_number!(0xFEB4, "WiSilica Inc."),
    assigned_number!(0xFEB5, "WiSilica Inc."),
    assigned_number!(0xFEB6, "Vencer Co, Ltd"),
    assigned_number!(0xFEB7, "Facebook, Inc."),
    assigned_number!(0xFEB8, "Facebook, Inc."),
    assigned_number!(0xFEB9, "LG Electronics"),
    assigned_number!(0xFEBA, "Tencent Holdings Limited"),
    assigned_number!(0xFEBB, "adafruit industries"),
    assigned_number!(0xFEBC, "Dexcom, Inc."),
    assigned_number!(0xFEBD, "Clover Network, Inc."),
    assigned_number!(0xFEBE, "Bose Corporation"),
    assigned_number!(0xFEBF, "Nod, Inc."),
    assigned_number!(0xFEC0, "KDDI Corporation"),
    assigned_number!(0xFEC1, "KDDI Corporation"),
    assigned_number!(0xFEC2, "Blue Spark Technologies, Inc."),
    assigned_number!(0xFEC3, "360fly, Inc."),
    assigned_number!(0xFEC4, "PLUS Location Systems"),
    assigned_number!(0xFEC5, "Realtek Semiconductor Corp."),
    assigned_number!(0xFEC6, "Kocomojo, LLC"),
    assigned_number!(0xFEC7, "Apple, Inc."),
    assigned_number!(0xFEC8, "Apple, Inc."),
    assigned_number!(0xFEC9, "Apple, Inc."),
    assigned_number!(0xFECA, "Apple, Inc."),
    assigned_number!(0xFECB, "Apple, Inc."),
    assigned_number!(0xFECC, "Apple, Inc."),
    assigned_number!(0xFECD, "Apple, Inc."),
    assigned_number!(0xFECE, "Apple, Inc."),
    assigned_number!(0xFECF, "Apple, Inc."),
    assigned_number!(0xFED0, "Apple, Inc."),
    assigned_number!(0xFED1, "Apple, Inc."),
    assigned_number!(0xFED2, "Apple, Inc."),
    assigned_number!(0xFED3, "Apple, Inc."),
    assigned_number!(0xFED4, "Apple, Inc."),
    assigned_number!(0xFED5, "Plantronics Inc."),
    assigned_number!(0xFED6, "Broadcom Corporation"),
    assigned_number!(0xFED7, "Broadcom Corporation"),
    assigned_number!(0xFED8, "Google Inc."),
    assigned_number!(0xFED9, "Pebble Technology Corporation"),
    assigned_number!(0xFEDA, "ISSC Technologies Corporation"),
    assigned_number!(0xFEDB, "Perka, Inc."),
    assigned_number!(0xFEDC, "Jawbone"),
    assigned_number!(0xFEDD, "Jawbone"),
    assigned_number!(0xFEDE, "Coin, Inc."),
    assigned_number!(0xFEDF, "Design SHIFT"),
    assigned_number!(0xFEE0, "Anhui Huami Information Technology Co."),
    assigned_number!(0xFEE1, "Anhui Huami Information Technology Co."),
    assigned_number!(0xFEE2, "Anki, Inc."),
    assigned_number!(0xFEE3, "Anki, Inc."),
    assigned_number!(0xFEE4, "Nordic Semiconductor ASA"),
    assigned_number!(0xFEE5, "Nordic Semiconductor ASA"),
    assigned_number!(0xFEE6, "Silvair, Inc."),
    assigned_number!(0xFEE7, "Tencent Holdings Limited"),
    assigned_number!(0xFEE8, "Quintic Corp."),
    assigned_number!(0xFEE9, "Quintic Corp."),
    assigned_number!(0xFEEA, "Swirl Networks, Inc."),
    assigned_number!(0xFEEB, "Swirl Networks, Inc."),
    assigned_number!(0xFEEC, "Tile, Inc."),
    assigned_number!(0xFEED, "Tile, Inc."),
    assigned_number!(0xFEEE, "Polar Electro Oy"),
    assigned_number!(0xFEEF, "Polar Electro Oy"),
    assigned_number!(0xFEF0, "Intel"),
    assigned_number!(0xFEF1, "CSR"),
    assigned_number!(0xFEF2, "CSR"),
    assigned_number!(0xFEF3, "Google Inc."),
    assigned_number!(0xFEF4, "Google Inc."),
    assigned_number!(0xFEF5, "Dialog Semiconductor GmbH"),
    assigned_number!(0xFEF6, "Wicentric, Inc."),
    assigned_number!(0xFEF7, "Aplix Corporation"),
    assigned_number!(0xFEF8, "Aplix Corporation"),
    assigned_number!(0xFEF9, "PayPal, Inc."),
    assigned_number!(0xFEFA, "PayPal, Inc."),
    assigned_number!(0xFEFB, "Telit Wireless Solutions (Formerly Stollmann E+V GmbH)"),
    assigned_number!(0xFEFC, "Gimbal, Inc."),
    assigned_number!(0xFEFD, "Gimbal, Inc."),
    assigned_number!(0xFEFE, "GN ReSound A/S"),
    assigned_number!(0xFEFF, "GN Netcom"),
];

pub(super) const CHARACTERISTIC_NUMBERS: [AssignedNumber; 226] = [
    assigned_number!(0x2A00, "Device Name"),
    assigned_number!(0x2A01, "Appearance"),
    assigned_number!(0x2A02, "Peripheral Privacy Flag"),
    assigned_number!(0x2A03, "Reconnection Address"),
    assigned_number!(0x2A04, "Peripheral Preferred Connection Parameters"),
    assigned_number!(0x2A05, "Service Changed"),
    assigned_number!(0x2A06, "Alert Level"),
    assigned_number!(0x2A07, "Tx Power Level"),
    assigned_number!(0x2A08, "Date Time"),
    assigned_number!(0x2A09, "Day of Week"),
    assigned_number!(0x2A0A, "Day Date Time"),
    assigned_number!(0x2A0B, "Exact Time 100"),
    assigned_number!(0x2A0C, "Exact Time 256"),
    assigned_number!(0x2A0D, "DST Offset"),
    assigned_number!(0x2A0E, "Time Zone"),
    assigned_number!(0x2A0F, "Local Time Information"),
    assigned_number!(0x2A10, "Secondary Time Zone"),
    assigned_number!(0x2A11, "Time with DST"),
    assigned_number!(0x2A12, "Time Accuracy"),
    assigned_number!(0x2A13, "Time Source"),
    assigned_number!(0x2A14, "Reference Time Information"),
    assigned_number!(0x2A15, "Time Broadcast"),
    assigned_number!(0x2A16, "Time Update Control Point"),
    assigned_number!(0x2A17, "Time Update State"),
    assigned_number!(0x2A18, "Glucose Measurement"),
    assigned_number!(0x2A19, "Battery Level"),
    assigned_number!(0x2A1A, "Battery Power State"),
    assigned_number!(0x2A1B, "Battery Level State"),
    assigned_number!(0x2A1C, "Temperature Measurement"),
    assigned_number!(0x2A1D, "Temperature Type"),
    assigned_number!(0x2A1E, "Intermediate Temperature"),
    assigned_number!(0x2A1F, "Temperature Celsius"),
    assigned_number!(0x2A20, "Temperature Fahrenheit"),
    assigned_number!(0x2A21, "Measurement Interval"),
    assigned_number!(0x2A22, "Boot Keyboard Input Report"),
    assigned_number!(0x2A23, "System ID"),
    assigned_number!(0x2A24, "Model Number String"),
    assigned_number!(0x2A25, "Serial Number String"),
    assigned_number!(0x2A26, "Firmware Revision String"),
    assigned_number!(0x2A27, "Hardware Revision String"),
    assigned_number!(0x2A28, "Software Revision String"),
    assigned_number!(0x2A29, "Manufacturer Name String"),
    assigned_number!(0x2A2A, "IEEE 11073-20601 Regulatory Certification Data List"),
    assigned_number!(0x2A2B, "Current Time"),
    assigned_number!(0x2A2C, "Magnetic Declination"),
    assigned_number!(0x2A2F, "Position 2D"),
    assigned_number!(0x2A30, "Position 3D"),
    assigned_number!(0x2A31, "Scan Refresh"),
    assigned_number!(0x2A32, "Boot Keyboard Output Report"),
    assigned_number!(0x2A33, "Boot Mouse Input Report"),
    assigned_number!(0x2A34, "Glucose Measurement Context"),
    assigned_number!(0x2A35, "Blood Pressure Measurement"),
    assigned_number!(0x2A36, "Intermediate Cuff Pressure"),
    assigned_number!(0x2A37, "Heart Rate Measurement"),
    assigned_number!(0x2A38, "Body Sensor Location"),
    assigned_number!(0x2A39, "Heart Rate Control Point"),
    assigned_number!(0x2A3A, "Removable"),
    assigned_number!(0x2A3B, "Service Required"),
    assigned_number!(0x2A3C, "Scientific Temperature Celsius"),
    assigned_number!(0x2A3D, "String"),
    assigned_number!(0x2A3E, "Network Availability"),
    assigned_number!(0x2A3F, "Alert Status"),
    assigned_number!(0x2A40, "Ringer Control point"),
    assigned_number!(0x2A41, "Ringer Setting"),
    assigned_number!(0x2A42, "Alert Category ID Bit Mask"),
    assigned_number!(0x2A43, "Alert Category ID"),
    assigned_number!(0x2A44, "Alert Notification Control Point"),
    assigned_number!(0x2A45, "Unread Alert Status"),
    assigned_number!(0x2A46, "New Alert"),
    assigned_number!(0x2A47, "Supported New Alert Category"),
    assigned_number!(0x2A48, "Supported Unread Alert Category"),
    assigned_number!(0x2A49, "Blood Pressure Feature"),
    assigned_number!(0x2A4A, "HID Information"),
    assigned_number!(0x2A4B, "Report Map"),
    assigned_number!(0x2A4C, "HID Control Point"),
    assigned_number!(0x2A4D, "Report"),
    assigned_number!(0x2A4E, "Protocol Mode"),
    assigned_number!(0x2A4F, "Scan Interval Window"),
    assigned_number!(0x2A50, "PnP ID"),
    assigned_number!(0x2A51, "Glucose Feature"),
    assigned_number!(0x2A52, "Record Access Control Point"),
    assigned_number!(0x2A53, "RSC Measurement"),
    assigned_number!(0x2A54, "RSC Feature"),
    assigned_number!(0x2A55, "SC Control Point"),
    assigned_number!(0x2A56, "Digital"),
    assigned_number!(0x2A57, "Digital Output"),
    assigned_number!(0x2A58, "Analog"),
    assigned_number!(0x2A59, "Analog Output"),
    assigned_number!(0x2A5A, "Aggregate"),
    assigned_number!(0x2A5B, "CSC Measurement"),
    assigned_number!(0x2A5C, "CSC Feature"),
    assigned_number!(0x2A5D, "Sensor Location"),
    assigned_number!(0x2A5E, "PLX Spot-Check Measurement"),
    assigned_number!(0x2A5F, "PLX Continuous Measurement Characteristic"),
    assigned_number!(0x2A60, "PLX Features"),
    assigned_number!(0x2A62, "Pulse Oximetry Control Point"),
    assigned_number!(0x2A63, "Cycling Power Measurement"),
    assigned_number!(0x2A64, "Cycling Power Vector"),
    assigned_number!(0x2A65, "Cycling Power Feature"),
    assigned_number!(0x2A66, "Cycling Power Control Point"),
    assigned_number!(0x2A67, "Location and Speed Characteristic"),
    assigned_number!(0x2A68, "Navigation"),
    assigned_number!(0x2A69, "Position Quality"),
    assigned_number!(0x2A6A, "LN Feature"),
    assigned_number!(0x2A6B, "LN Control Point"),
    assigned_number!(0x2A6C, "Elevation"),
    assigned_number!(0x2A6D, "Pressure"),
    assigned_number!(0x2A6E, "Temperature"),
    assigned_number!(0x2A6F, "Humidity"),
    assigned_number!(0x2A70, "True Wind Speed"),
    assigned_number!(0x2A71, "True Wind Direction"),
    assigned_number!(0x2A72, "Apparent Wind Speed"),
    assigned_number!(0x2A73, "Apparent Wind Direction"),
    assigned_number!(0x2A74, "Gust Factor"),
    assigned_number!(0x2A75, "Pollen Concentration"),
    assigned_number!(0x2A76, "UV Index"),
    assigned_number!(0x2A77, "Irradiance"),
    assigned_number!(0x2A78, "Rainfall"),
    assigned_number!(0x2A79, "Wind Chill"),
    assigned_number!(0x2A7A, "Heat Index"),
    assigned_number!(0x2A7B, "Dew Point"),
    assigned_number!(0x2A7D, "Descriptor Value Changed"),
    assigned_number!(0x2A7E, "Aerobic Heart Rate Lower Limit"),
    assigned_number!(0x2A7F, "Aerobic Threshold"),
    assigned_number!(0x2A80, "Age"),
    assigned_number!(0x2A81, "Anaerobic Heart Rate Lower Limit"),
    assigned_number!(0x2A82, "Anaerobic Heart Rate Upper Limit"),
    assigned_number!(0x2A83, "Anaerobic Threshold"),
    assigned_number!(0x2A84, "Aerobic Heart Rate Upper Limit"),
    assigned_number!(0x2A85, "Date of Birth"),
    assigned_number!(0x2A86, "Date of Threshold Assessment"),
    assigned_number!(0x2A87, "Email Address"),
    assigned_number!(0x2A88, "Fat Burn Heart Rate Lower Limit"),
    assigned_number!(0x2A89, "Fat Burn Heart Rate Upper Limit"),
    assigned_number!(0x2A8A, "First Name"),
    assigned_number!(0x2A8B, "Five Zone Heart Rate Limits"),
    assigned_number!(0x2A8C, "Gender"),
    assigned_number!(0x2A8D, "Heart Rate Max"),
    assigned_number!(0x2A8E, "Height"),
    assigned_number!(0x2A8F, "Hip Circumference"),
    assigned_number!(0x2A90, "Last Name"),
    assigned_number!(0x2A91, "Maximum Recommended Heart Rate"),
    assigned_number!(0x2A92, "Resting Heart Rate"),
    assigned_number!(0x2A93, "Sport Type for Aerobic and Anaerobic Thresholds"),
    assigned_number!(0x2A94, "Three Zone Heart Rate Limits"),
    assigned_number!(0x2A95, "Two Zone Heart Rate Limit"),
    assigned_number!(0x2A96, "VO2 Max"),
    assigned_number!(0x2A97, "Waist Circumference"),
    assigned_number!(0x2A98, "Weight"),
    assigned_number!(0x2A99, "Database Change Increment"),
    assigned_number!(0x2A9A, "User Index"),
    assigned_number!(0x2A9B, "Body Composition Feature"),
    assigned_number!(0x2A9C, "Body Composition Measurement"),
    assigned_number!(0x2A9D, "Weight Measurement"),
    assigned_number!(0x2A9E, "Weight Scale Feature"),
    assigned_number!(0x2A9F, "User Control Point"),
    assigned_number!(0x2AA0, "Magnetic Flux Density - 2D"),
    assigned_number!(0x2AA1, "Magnetic Flux Density - 3D"),
    assigned_number!(0x2AA2, "Language"),
    assigned_number!(0x2AA3, "Barometric Pressure Trend"),
    assigned_number!(0x2AA4, "Bond Management Control Point"),
    assigned_number!(0x2AA5, "Bond Management Features"),
    assigned_number!(0x2AA6, "Central Address Resolution"),
    assigned_number!(0x2AA7, "CGM Measurement"),
    assigned_number!(0x2AA8, "CGM Feature"),
    assigned_number!(0x2AA9, "CGM Status"),
    assigned_number!(0x2AAA, "CGM Session Start Time"),
    assigned_number!(0x2AAB, "CGM Session Run Time"),
    assigned_number!(0x2AAC, "CGM Specific Ops Control Point"),
    assigned_number!(0x2AAD, "Indoor Positioning Configuration"),
    assigned_number!(0x2AAE, "Latitude"),
    assigned_number!(0x2AAF, "Longitude"),
    assigned_number!(0x2AB0, "Local North Coordinate"),
    assigned_number!(0x2AB1, "Local East Coordinate"),
    assigned_number!(0x2AB2, "Floor Number"),
    assigned_number!(0x2AB3, "Altitude"),
    assigned_number!(0x2AB4, "Uncertainty"),
    assigned_number!(0x2AB5, "Location Name"),
    assigned_number!(0x2AB6, "URI"),
    assigned_number!(0x2AB7, "HTTP Headers"),
    assigned_number!(0x2AB8, "HTTP Status Code"),
    assigned_number!(0x2AB9, "HTTP Entity Body"),
    assigned_number!(0x2ABA, "HTTP Control Point"),
    assigned_number!(0x2ABB, "HTTPS Security"),
    assigned_number!(0x2ABC, "TDS Control Point"),
    assigned_number!(0x2ABD, "OTS Feature"),
    assigned_number!(0x2ABE, "Object Name"),
    assigned_number!(0x2ABF, "Object Type"),
    assigned_number!(0x2AC0, "Object Size"),
    assigned_number!(0x2AC1, "Object First-Created"),
    assigned_number!(0x2AC2, "Object Last-Modified"),
    assigned_number!(0x2AC3, "Object ID"),
    assigned_number!(0x2AC4, "Object Properties"),
    assigned_number!(0x2AC5, "Object Action Control Point"),
    assigned_number!(0x2AC6, "Object List Control Point"),
    assigned_number!(0x2AC7, "Object List Filter"),
    assigned_number!(0x2AC8, "Object Changed"),
    assigned_number!(0x2AC9, "Resolvable Private Address Only"),
    assigned_number!(0x2ACC, "Fitness Machine Feature"),
    assigned_number!(0x2ACD, "Treadmill Data"),
    assigned_number!(0x2ACE, "Cross Trainer Data"),
    assigned_number!(0x2ACF, "Step Climber Data"),
    assigned_number!(0x2AD0, "Stair Climber Data"),
    assigned_number!(0x2AD1, "Rower Data"),
    assigned_number!(0x2AD2, "Indoor Bike Data"),
    assigned_number!(0x2AD3, "Training Status"),
    assigned_number!(0x2AD4, "Supported Speed Range"),
    assigned_number!(0x2AD5, "Supported Inclination Range"),
    assigned_number!(0x2AD6, "Supported Resistance Level Range"),
    assigned_number!(0x2AD7, "Supported Heart Rate Range"),
    assigned_number!(0x2AD8, "Supported Power Range"),
    assigned_number!(0x2AD9, "Fitness Machine Control Point"),
    assigned_number!(0x2ADA, "Fitness Machine Status"),
    assigned_number!(0x2AED, "Date UTC"),
    assigned_number!(0x2B1D, "RC Feature"),
    assigned_number!(0x2B1E, "RC Settings"),
    assigned_number!(0x2B1F, "Reconnection Configuration Control Point"),
    assigned_number!(0x2B20, "IDD Status Changed"),
    assigned_number!(0x2B21, "IDD Status"),
    assigned_number!(0x2B22, "IDD Annunciation Status"),
    assigned_number!(0x2B23, "IDD Features"),
    assigned_number!(0x2B24, "IDD Status Reader Control Point"),
    assigned_number!(0x2B25, "IDD Command Control Point"),
    assigned_number!(0x2B26, "IDD Command Data"),
    assigned_number!(0x2B27, "IDD Record Access Control Point"),
    assigned_number!(0x2B28, "IDD History Data"),
];

pub(super) const DESCRIPTOR_NUMBERS: [AssignedNumber; 15] = [
    assigned_number!(0x2900, "Characteristic Extended Properties"),
    assigned_number!(0x2901, "Characteristic User Description"),
    assigned_number!(0x2902, "Client Characteristic Configuration"),
    assigned_number!(0x2903, "Server Characteristic Configuration"),
    assigned_number!(0x2904, "Characteristic Presentation Format"),
    assigned_number!(0x2905, "Characteristic Aggregate Format"),
    assigned_number!(0x2906, "Valid Range"),
    assigned_number!(0x2907, "External Report Reference"),
    assigned_number!(0x2908, "Report Reference"),
    assigned_number!(0x2909, "Number of Digitals"),
    assigned_number!(0x290A, "Value Trigger Setting"),
    assigned_number!(0x290B, "Environmental Sensing Configuration"),
    assigned_number!(0x290C, "Environmental Sensing Measurement"),
    assigned_number!(0x290D, "Environmental Sensing Trigger Setting"),
    assigned_number!(0x290E, "Time Trigger Setting"),
];

#[allow(clippy::invisible_characters)] // TODO(fxbug.dev/95033)
/// Service Class Profile Identifiers.
/// Used in SDP to advertise the type of service being provided.
/// Source: https://www.bluetooth.com/specifications/assigned-numbers/service-discovery/
pub(crate) const SERVICE_CLASS_UUIDS: [AssignedNumber; 76] = [
    assigned_number!(0x1000, "ServiceDiscoveryServerServiceClassID"),
    assigned_number!(0x1001, "BrowseGroupDescriptorServiceClassID"),
    assigned_number!(0x1101, "SerialPort"),
    assigned_number!(0x1102, "LANAccessUsingPPP"),
    assigned_number!(0x1103, "DialupNetworking"),
    assigned_number!(0x1104, "IrMCSync"),
    assigned_number!(0x1105, "OBEXObjectPush"),
    assigned_number!(0x1106, "OBEXFileTransfer"),
    assigned_number!(0x1107, "IrMCSyncCommand"),
    assigned_number!(0x1108, "Headset"),
    assigned_number!(0x1109, "CordlessTelephony"),
    assigned_number!(0x110A, "AudioSource"),
    assigned_number!(0x110B, "AudioSink"),
    assigned_number!(0x110C, "A/V_RemoteControlTarget"),
    assigned_number!(0x110D, "AdvancedAudioDistribution"),
    assigned_number!(0x110E, "A/V_RemoteControl"),
    assigned_number!(0x110F, "A/V_RemoteControlController"),
    assigned_number!(0x1110, "Intercom"),
    assigned_number!(0x1111, "Fax"),
    assigned_number!(0x1112, "Headset – Audio Gateway (AG)"),
    assigned_number!(0x1113, "WAP"),
    assigned_number!(0x1114, "WAP_CLIENT"),
    assigned_number!(0x1115, "PANU"),
    assigned_number!(0x1116, "NAP"),
    assigned_number!(0x1117, "GN"),
    assigned_number!(0x1118, "DirectPrinting"),
    assigned_number!(0x1119, "ReferencePrinting"),
    assigned_number!(0x111A, "Basic Imaging Profile"),
    assigned_number!(0x111B, "ImagingResponder"),
    assigned_number!(0x111C, "ImagingAutomaticArchive"),
    assigned_number!(0x111D, "ImagingReferencedObjects"),
    assigned_number!(0x111E, "Handsfree"),
    assigned_number!(0x111F, "HandsfreeAudioGateway"),
    assigned_number!(0x1120, "DirectPrintingReferenceObjectsService"),
    assigned_number!(0x1121, "ReflectedUI"),
    assigned_number!(0x1122, "BasicPrinting"),
    assigned_number!(0x1123, "PrintingStatus"),
    assigned_number!(0x1124, "HumanInterfaceDeviceService"),
    assigned_number!(0x1125, "HardcopyCableReplacement"),
    assigned_number!(0x1126, "HCR_Print"),
    assigned_number!(0x1127, "HCR_Scan"),
    assigned_number!(0x1128, "Common_ISDN_Access"),
    assigned_number!(0x112D, "SIM_Access"),
    assigned_number!(0x112E, "Phonebook Access – PCE"),
    assigned_number!(0x112F, "Phonebook Access – PSE"),
    assigned_number!(0x1130, "Phonebook Access"),
    assigned_number!(0x1131, "Headset – HS"),
    assigned_number!(0x1132, "Message Access Server"),
    assigned_number!(0x1133, "Message Notification Server"),
    assigned_number!(0x1134, "Message Access Profile"),
    assigned_number!(0x1135, "GNSS"),
    assigned_number!(0x1136, "GNSS_Server"),
    assigned_number!(0x1137, "3D Display"),
    assigned_number!(0x1138, "3D Glasses"),
    assigned_number!(0x1139, "3D Synchronization"),
    assigned_number!(0x113A, "MPS Profile UUID"),
    assigned_number!(0x113B, "MPS SC UUID"),
    assigned_number!(0x113C, "CTN Access Service​"),
    assigned_number!(0x113D, "CTN Notification Service​"),
    assigned_number!(0x113E, "CTN Profile"),
    assigned_number!(0x1200, "PnPInformation"),
    assigned_number!(0x1201, "GenericNetworking"),
    assigned_number!(0x1202, "GenericFileTransfer"),
    assigned_number!(0x1203, "GenericAudio"),
    assigned_number!(0x1204, "GenericTelephony"),
    assigned_number!(0x1205, "UPNP_Service"),
    assigned_number!(0x1206, "UPNP_IP_Service"),
    assigned_number!(0x1300, "ESDP_UPNP_IP_PAN"),
    assigned_number!(0x1301, "ESDP_UPNP_IP_LAP"),
    assigned_number!(0x1302, "ESDP_UPNP_L2CAP"),
    assigned_number!(0x1303, "VideoSource"),
    assigned_number!(0x1304, "VideoSink"),
    assigned_number!(0x1305, "VideoDistribution"),
    assigned_number!(0x1400, "HDP"),
    assigned_number!(0x1401, "HDP Source"),
    assigned_number!(0x1402, "HDP Sink"),
];
