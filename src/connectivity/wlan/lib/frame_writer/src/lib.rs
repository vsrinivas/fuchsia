// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro_hack::proc_macro_hack;

#[proc_macro_hack]
pub use wlan_frame_writer_macro::write_frame;
#[proc_macro_hack]
pub use wlan_frame_writer_macro::write_frame_with_dynamic_buf;
#[proc_macro_hack]
pub use wlan_frame_writer_macro::write_frame_with_fixed_buf;

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::fmt,
        thiserror::Error,
        wlan_common::{
            self as common,
            appendable::BufferTooSmall,
            ie::{
                self,
                rsn::{
                    akm::{Akm, PSK},
                    cipher::{Cipher, CCMP_128, TKIP},
                    rsne,
                },
                wpa,
            },
            mac::*,
            organization::Oui,
        },
    };

    #[derive(Debug, Error, PartialEq, Eq, Ord, PartialOrd, Hash)]
    pub enum Error {
        FrameWriteError,
        BufferTooSmall,
    }

    impl fmt::Display for Error {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "{:?}", self)
        }
    }

    impl From<BufferTooSmall> for Error {
        fn from(_: BufferTooSmall) -> Self {
            Error::BufferTooSmall
        }
    }

    impl From<common::error::FrameWriteError> for Error {
        fn from(_: common::error::FrameWriteError) -> Self {
            Error::FrameWriteError
        }
    }

    struct BufferProvider;
    impl BufferProvider {
        fn get_buffer(&mut self, min_len: usize) -> Result<Vec<u8>, Error> {
            Ok(vec![0; min_len])
        }
    }

    fn make_mgmt_hdr() -> MgmtHdr {
        MgmtHdr {
            frame_ctrl: FrameControl(0x4321),
            duration: 42,
            addr1: [7; 6],
            addr2: [6; 6],
            addr3: [5; 6],
            seq_ctrl: SequenceControl(0x8765),
        }
    }

    #[test]
    fn write_emit_offset_buf_provider() {
        let buffer_provider = BufferProvider;
        let mut offset = 0;
        write_frame!(buffer_provider, {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8],
                offset @ extended_supported_rates: &[1u8, 2, 3, 4]
            }
        })
        .expect("frame construction failed");
        assert_eq!(offset, 10);
    }

    #[test]
    fn write_emit_offset_buf() {
        let mut offset = 0;
        write_frame_with_dynamic_buf!(vec![], {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8],
                offset @ extended_supported_rates: &[1u8, 2, 3, 4]
            }
        })
        .expect("frame construction failed");
        assert_eq!(offset, 10);
    }

    #[test]
    fn write_buf() {
        let (buf, bytes_written) = write_frame_with_fixed_buf!(vec![0u8; 10], {
            ies: { ssid: &b"foobar"[..] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 8);
        assert_eq!(&[0, 6, 102, 111, 111, 98, 97, 114, 0, 0][..], &buf[..]);
    }

    #[test]
    fn write_buf_empty_vec() {
        let (buf, bytes_written) = write_frame_with_dynamic_buf!(vec![], {
            ies: { ssid: &b"foobar"[..] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 8);
        assert_eq!(&[0, 6, 102, 111, 111, 98, 97, 114,][..], &buf[..]);
    }

    #[test]
    fn write_fixed_buf() {
        let (buf, bytes_written) = write_frame_with_fixed_buf!([0u8; 10], {
            ies: { ssid: &b"foobar"[..] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 8);
        assert_eq!(&[0, 6, 102, 111, 111, 98, 97, 114,][..], &buf[..bytes_written]);
    }

    #[test]
    fn write_ssid() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { ssid: &b"foobar"[..] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 8);
        assert_eq!(&[0, 6, 102, 111, 111, 98, 97, 114,][..], &buf[..]);
    }

    #[test]
    fn write_ssid_empty() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { ssid: [0u8; 0] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 2);
        assert_eq!(&[0, 0][..], &buf[..]);
    }

    #[test]
    fn write_ssid_max() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { ssid: [2u8; 32] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 34);
        #[rustfmt::skip]
        assert_eq!(
            &[
                0, 32,
                2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2,
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_ssid_too_large() {
        let buffer_provider = BufferProvider;
        let err = write_frame!(buffer_provider, {
            ies: { ssid: [2u8; 33] }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_tim() {
        let (buf, bytes_written) = write_frame_with_dynamic_buf!(vec![], {
            ies: {
                tim: ie::TimView {
                    header: ie::TimHeader {
                        dtim_count: 1,
                        dtim_period: 2,
                        bmp_ctrl: ie::BitmapControl(3)
                    },
                    bitmap: &[4, 5, 6][..],
                }
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 8);
        assert_eq!(&[5, 6, 1, 2, 3, 4, 5, 6][..], &buf[..]);
    }

    #[test]
    fn write_tim_empty_bitmap() {
        let err = write_frame_with_dynamic_buf!(vec![], {
            ies: {
                tim: ie::TimView {
                    header: ie::TimHeader {
                        dtim_count: 1,
                        dtim_period: 2,
                        bmp_ctrl: ie::BitmapControl(3)
                    },
                    bitmap: &[][..],
                }
            }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_tim_bitmap_too_long() {
        let err = write_frame_with_dynamic_buf!(vec![], {
            ies: {
                tim: ie::TimView {
                    header: ie::TimHeader {
                        dtim_count: 1,
                        dtim_period: 2,
                        bmp_ctrl: ie::BitmapControl(3)
                    },
                    bitmap: &[0xFF_u8; 252][..],
                }
            }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_rates() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { supported_rates: &[1u8, 2, 3, 4, 5] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 7);
        assert_eq!(&[1, 5, 1, 2, 3, 4, 5,][..], &buf[..]);
    }

    #[test]
    fn write_rates_too_large() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9] }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 10);
        assert_eq!(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8][..], &buf[..]);
    }

    #[test]
    fn write_rates_empty() {
        let buffer_provider = BufferProvider;
        let err = write_frame!(buffer_provider, {
            ies: { supported_rates: &[] }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_extended_supported_rates_too_few_rates() {
        let buffer_provider = BufferProvider;
        let err = write_frame!(buffer_provider, {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6],
                extended_supported_rates: &[1u8, 2, 3, 4]
            }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_extended_supported_rates_too_many_rates() {
        let buffer_provider = BufferProvider;
        let err = write_frame!(buffer_provider, {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9],
                extended_supported_rates: &[1u8, 2, 3, 4]
            }
        })
        .expect_err("frame construction succeeded");
        assert_eq!(err, Error::FrameWriteError);
    }

    #[test]
    fn write_extended_supported_rates_continued() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9],
                extended_supported_rates: {/* continue rates */}
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 13);
        assert_eq!(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8, 50, 1, 9][..], &buf[..]);
    }

    #[test]
    fn write_extended_supported_rates_separate() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8],
                extended_supported_rates: &[11u8, 12, 13],
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 15);
        assert_eq!(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8, 50, 3, 11, 12, 13][..], &buf[..]);
    }

    #[test]
    fn write_rsne() {
        let rsne = rsne::Rsne::wpa2_psk_ccmp_rsne();

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { rsne: &rsne, }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 20);
        assert_eq!(
            &[
                48, 18, // Element header
                1, 0, // Version
                0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
                1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
                1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_wpa1() {
        let wpa_ie = wpa::WpaIe {
            multicast_cipher: Cipher { oui: Oui::MSFT, suite_type: TKIP },
            unicast_cipher_list: vec![Cipher { oui: Oui::MSFT, suite_type: TKIP }],
            akm_list: vec![Akm { oui: Oui::MSFT, suite_type: PSK }],
        };

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: { wpa1: &wpa_ie, }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 24);
        assert_eq!(
            &[
                0xdd, 0x16, // Vendor IE header
                0x00, 0x50, 0xf2, // MSFT OUI
                0x01, 0x01, 0x00, // WPA IE header
                0x00, 0x50, 0xf2, 0x02, // multicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_match_optional_positive() {
        let wpa_ie = wpa::WpaIe {
            multicast_cipher: Cipher { oui: Oui::MSFT, suite_type: TKIP },
            unicast_cipher_list: vec![Cipher { oui: Oui::MSFT, suite_type: TKIP }],
            akm_list: vec![Akm { oui: Oui::MSFT, suite_type: PSK }],
        };

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                wpa1?: match 2u8 {
                    1 => None,
                    2 => Some(&wpa_ie),
                    _ => None,
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 24);
        assert_eq!(
            &[
                0xdd, 0x16, // Vendor IE header
                0x00, 0x50, 0xf2, // MSFT OUI
                0x01, 0x01, 0x00, // WPA IE header
                0x00, 0x50, 0xf2, 0x02, // multicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_match_optional_negative() {
        let wpa_ie = wpa::WpaIe {
            multicast_cipher: Cipher { oui: Oui::MSFT, suite_type: TKIP },
            unicast_cipher_list: vec![Cipher { oui: Oui::MSFT, suite_type: TKIP }],
            akm_list: vec![Akm { oui: Oui::MSFT, suite_type: PSK }],
        };

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                wpa1?: match 1u8 {
                    1 => None,
                    2 => Some(&wpa_ie),
                    _ => None,
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 0);
        assert!(buf.is_empty());
    }

    #[test]
    fn write_match_required() {
        let wpa_ie_first = wpa::WpaIe {
            multicast_cipher: Cipher { oui: Oui::MSFT, suite_type: TKIP },
            unicast_cipher_list: vec![Cipher { oui: Oui::MSFT, suite_type: TKIP }],
            akm_list: vec![Akm { oui: Oui::MSFT, suite_type: PSK }],
        };
        let wpa_ie_second = wpa::WpaIe {
            multicast_cipher: Cipher { oui: Oui::MSFT, suite_type: CCMP_128 },
            unicast_cipher_list: vec![Cipher { oui: Oui::MSFT, suite_type: CCMP_128 }],
            akm_list: vec![Akm { oui: Oui::MSFT, suite_type: PSK }],
        };

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                wpa1: match 1u8 {
                    1 => &wpa_ie_first,
                    _ => &wpa_ie_second,
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 24);
        assert_eq!(
            &[
                0xdd, 0x16, // Vendor IE header
                0x00, 0x50, 0xf2, // MSFT OUI
                0x01, 0x01, 0x00, // WPA IE header
                0x00, 0x50, 0xf2, 0x02, // multicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 unicast cipher: TKIP
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
            ][..],
            &buf[..]
        );

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                wpa1: match 2u8 {
                    1 => &wpa_ie_first,
                    _ => &wpa_ie_second,
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 24);
        assert_eq!(
            &[
                0xdd, 0x16, // Vendor IE header
                0x00, 0x50, 0xf2, // MSFT OUI
                0x01, 0x01, 0x00, // WPA IE header
                0x00, 0x50, 0xf2, 0x04, // multicast cipher: CCMP_128
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x04, // 1 unicast cipher: CCMP_128
                0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_ht_caps() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                ht_cap: &ie::HtCapabilities {
                    ht_cap_info: ie::HtCapabilityInfo(0x1234),
                    ampdu_params: ie::AmpduParams(42),
                    mcs_set: ie::SupportedMcsSet(0x1200_3400_5600_7800_9000_1200_3400_5600),
                    ht_ext_cap: ie::HtExtCapabilities(0x1234),
                    txbf_cap: ie::TxBfCapability(0x12345678),
                    asel_cap: ie::AselCapability(43),
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 28);
        assert_eq!(
            &[
                45, 26, // Element header
                0x34, 0x12, // ht_cap_info
                42,   // ampdu_params
                0, 0x56, 0, 0x34, 0, 0x12, 0, 0x90, 0, 0x78, 0, 0x56, 0, 0x34, 0,
                0x12, // mcs_set
                0x34, 0x12, // ht_ext_cap
                0x78, 0x56, 0x34, 0x12, // txbf_cap
                43,   // asel_cap
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_vht_caps() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                vht_cap: &ie::VhtCapabilities {
                    vht_cap_info: ie::VhtCapabilitiesInfo(0x1200_3400),
                    vht_mcs_nss: ie::VhtMcsNssSet(0x1200_3400_5600_7800),
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 14);
        assert_eq!(
            &[
                191, 12, // Element header
                0, 0x34, 0, 0x12, // vht_cap_info
                0, 0x78, 0, 0x56, 0, 0x34, 0, 0x12, // vht_mcs_nss
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_dsss_param_set() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                dsss_param_set: &ie::DsssParamSet {
                    current_chan: 42
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 3);
        assert_eq!(&[3, 1, 42][..], &buf[..]);
    }

    #[test]
    fn write_bss_max_idle_period() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                bss_max_idle_period: &ie::BssMaxIdlePeriod {
                    max_idle_period: 42,
                    idle_options: ie::IdleOptions(8),
                },
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 5);
        assert_eq!(&[90, 3, 42, 0, 8,][..], &buf[..]);
    }

    #[test]
    fn write_fields() {
        // Some expression which can't be statically evaluated but always returns true.
        let v = vec![5; 5];
        let always_true = v.len() < 6;
        let mut ht_capabilities = None;
        if !always_true {
            ht_capabilities = Some(ie::HtCapabilities {
                ht_cap_info: ie::HtCapabilityInfo(0x1234),
                ampdu_params: ie::AmpduParams(42),
                mcs_set: ie::SupportedMcsSet(0x1200_3400_5600_7800_9000_1200_3400_5600),
                ht_ext_cap: ie::HtExtCapabilities(0x1234),
                txbf_cap: ie::TxBfCapability(0x12345678),
                asel_cap: ie::AselCapability(43),
            });
        }

        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            ies: {
                ssid: if always_true { &[2u8; 2][..] } else { &[2u8; 33][..] },
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9],
                ht_cap?: ht_capabilities,
                vht_cap?: if always_true {
                    &ie::VhtCapabilities {
                        vht_cap_info: ie::VhtCapabilitiesInfo(0x1200_3400),
                        vht_mcs_nss: ie::VhtMcsNssSet(0x1200_3400_5600_7800),
                    }
                },
                extended_supported_rates: {},
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 31);
        #[rustfmt::skip]
        assert_eq!(
            &[
                0, 2, 2, 2, // SSID
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // rates
                191, 12, // VHT Element header
                0, 0x34, 0, 0x12, // vht_cap_info
                0, 0x78, 0, 0x56, 0, 0x34, 0, 0x12, // vht_mcs_nss
                50, 1, 9, // extended rates
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_headers() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            headers: {
                // Struct expressions:
                MgmtHdr: &MgmtHdr {
                    frame_ctrl: FrameControl(0x1234),
                    duration: 42,
                    addr1: [7; 6],
                    addr2: [6; 6],
                    addr3: [5; 6],
                    seq_ctrl: SequenceControl(0x5678),
                },
                // Block expression:
                DeauthHdr: {
                    &DeauthHdr { reason_code: ReasonCode::MIC_FAILURE }
                },
                // Repeat and literal expressions:
                MacAddr: &[2u8; 6],
                u8: &42u8,
                // Function invocation:
                MgmtHdr: &make_mgmt_hdr(),
            }
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 57);
        #[rustfmt::skip]
        assert_eq!(
            &[
                // Struct expression: MgmtHdr
                0x34, 0x12, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x78, 0x56,
                // Struct expression: DeauthHdr
                14, 0,
                // Repeat and literal expressions:
                2, 2, 2, 2, 2, 2,
                42,
                // Function call: MgmtHdr
                0x21, 0x43, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x65, 0x87,
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_body() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            body: &[9u8; 9],
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 9);
        assert_eq!(&[9, 9, 9, 9, 9, 9, 9, 9, 9][..], &buf[..]);
    }

    #[test]
    fn write_payload() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            payload: &[9u8; 9],
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 9);
        assert_eq!(&[9, 9, 9, 9, 9, 9, 9, 9, 9][..], &buf[..]);
    }

    #[test]
    fn write_complex() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            headers: {
                MgmtHdr: &MgmtHdr {
                    frame_ctrl: FrameControl(0x1234),
                    duration: 42,
                    addr1: [7; 6],
                    addr2: [6; 6],
                    addr3: [5; 6],
                    seq_ctrl: SequenceControl(0x5678),
                },
                DeauthHdr: {
                    &DeauthHdr { reason_code: ReasonCode::MIC_FAILURE }
                },
                MacAddr: &[2u8; 6],
                u8: &42u8,
                MgmtHdr: &make_mgmt_hdr(),
            },
            body: vec![41u8; 3],
            ies: {
                ssid: &[2u8; 2][..],
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9],
                vht_cap: &ie::VhtCapabilities {
                    vht_cap_info: ie::VhtCapabilitiesInfo(0x1200_3400),
                    vht_mcs_nss: ie::VhtMcsNssSet(0x1200_3400_5600_7800),
                },
                extended_supported_rates: {},
            },
            payload: vec![42u8; 5]
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 96);
        #[rustfmt::skip]
        assert_eq!(
            &[
                // Headers:
                0x34, 0x12, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x78, 0x56,
                14, 0,
                2, 2, 2, 2, 2, 2,
                42,
                0x21, 0x43, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x65, 0x87,
                // Body:
                41, 41, 41,
                // Fields:
                0, 2, 2, 2, // SSID
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // rates
                191, 12, // VHT Element header
                0, 0x34, 0, 0x12, // vht_cap_info
                0, 0x78, 0, 0x56, 0, 0x34, 0, 0x12, // vht_mcs_nss
                50, 1, 9, // extended rates
                // Payload:
                42, 42, 42, 42, 42,
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_complex_verify_order() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) = write_frame!(buffer_provider, {
            payload: vec![42u8; 5],
            ies: {
                ssid: &[2u8; 2][..],
                supported_rates: &[1u8, 2, 3, 4, 5, 6, 7, 8, 9],
                vht_cap: &ie::VhtCapabilities {
                    vht_cap_info: ie::VhtCapabilitiesInfo(0x1200_3400),
                    vht_mcs_nss: ie::VhtMcsNssSet(0x1200_3400_5600_7800),
                },
                extended_supported_rates: {},
            },
            body: vec![41u8; 3],
            headers: {
                MgmtHdr: &MgmtHdr {
                    frame_ctrl: FrameControl(0x1234),
                    duration: 42,
                    addr1: [7; 6],
                    addr2: [6; 6],
                    addr3: [5; 6],
                    seq_ctrl: SequenceControl(0x5678),
                },
                DeauthHdr: {
                    &DeauthHdr { reason_code: ReasonCode::MIC_FAILURE }
                },
                MacAddr: &[2u8; 6],
                u8: &42u8,
                MgmtHdr: &make_mgmt_hdr(),
            },
        })
        .expect("frame construction failed");
        assert_eq!(bytes_written, 96);
        #[rustfmt::skip]
        assert_eq!(
            &[
                // Headers:
                0x34, 0x12, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x78, 0x56,
                14, 0,
                2, 2, 2, 2, 2, 2,
                42,
                0x21, 0x43, 42, 0, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 0x65, 0x87,
                // Body:
                41, 41, 41,
                // Fields:
                0, 2, 2, 2, // SSID
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // rates
                191, 12, // VHT Element header
                0, 0x34, 0, 0x12, // vht_cap_info
                0, 0x78, 0, 0x56, 0, 0x34, 0, 0x12, // vht_mcs_nss
                50, 1, 9, // extended rates
                // Payload:
                42, 42, 42, 42, 42,
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn write_nothing() {
        let buffer_provider = BufferProvider;
        let (buf, bytes_written) =
            write_frame!(buffer_provider, {}).expect("frame construction failed");
        assert_eq!(bytes_written, 0);
        assert_eq!(buf.len(), bytes_written);
    }
}
