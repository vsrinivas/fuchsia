// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Element;
use crate::ProtectionInfo;
use bitfield::bitfield;
use nom::error::ErrorKind;
use nom::number::streaming::{le_u16, le_u8};
use nom::{call, do_parse, eof, map, named, named_args, take, try_parse, IResult};
use wlan_common::{
    appendable::{Appendable, BufferTooSmall},
    ie::wpa,
    ie::write_wpa1_ie,
    organization::Oui,
};

pub const TYPE: u8 = 0xDD;
const PADDING_DATA_LEN: u8 = 0;
const HDR_LEN: usize = 6;
/// Octets taken by OUI and data type.
const HDR_OUI_TYPE_LEN: usize = 4;

// IEEE Std 802.11-2016 Table 12.6
const GTK_DATA_TYPE: u8 = 1;
const IGTK_DATA_TYPE: u8 = 9;

/// A GTK KDE's fixed length.
/// Note: The KDE consists of a fixed and variable length (the GTK).
const GTK_FIXED_LEN: usize = 2;

const IGTK_IPN_LEN: usize = 6;
const IGTK_FIXED_LEN: usize = 2 + IGTK_IPN_LEN;

// IEEE Std 802.11-2016, 12.7.2, Figure 12-34
#[derive(Default, Debug, PartialEq)]
pub struct Header {
    pub type_: u8,
    pub len: u8,
    pub oui: Oui,
    pub data_type: u8,
}

impl Header {
    pub fn new(type_: u8, len: u8, oui: &[u8], data_type: u8) -> Header {
        let mut oui_buf = [0u8; 3];
        oui_buf.copy_from_slice(oui);
        Header { type_, len, data_type, oui: Oui::new(oui_buf), ..Default::default() }
    }

    pub fn new_dot11(data_type: u8, data_len: usize) -> Header {
        Header { type_: TYPE, len: (HDR_OUI_TYPE_LEN + data_len) as u8, data_type, oui: Oui::DOT11 }
    }

    fn data_len(&self) -> usize {
        if self.len < 4 {
            0
        } else {
            (self.len as usize) - 4
        }
    }
}

// IEEE Std 802.11-2016, 12.7.2, j)
pub enum GtkInfoTx {
    _OnlyRx = 0,
    BothRxTx = 1,
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
bitfield! {
    pub struct GtkInfo(u8);
    impl Debug;
    pub key_id, set_key_id: 1, 0;
    pub tx, set_tx: 2, 2;
    // Bit 3-7 reserved.
    pub value, _: 7,0;
}

impl PartialEq for GtkInfo {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

/// GTK KDE:
/// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
#[derive(Debug, PartialEq)]
pub struct Gtk {
    pub info: GtkInfo,
    // 1 byte reserved.
    pub gtk: Vec<u8>,
}

impl Gtk {
    pub fn new(key_id: u8, tx: GtkInfoTx, gtk: &[u8]) -> Self {
        let mut gtk_info = GtkInfo(0);
        gtk_info.set_key_id(key_id);
        gtk_info.set_tx(tx as u8);
        Self { info: gtk_info, gtk: gtk.to_vec() }
    }

    /// Length of the GTK KDE including its fixed fields, not just the GTK.
    pub fn len(&self) -> usize {
        GTK_FIXED_LEN + self.gtk.len()
    }
}

/// IGTK KDE:
/// IEEE Std 802.11-2016, 12.7.2, Figure 12-42
#[derive(Debug, PartialEq)]
pub struct Igtk {
    pub id: u16,
    // IGTK Packet Number
    pub ipn: [u8; IGTK_IPN_LEN],
    pub igtk: Vec<u8>,
}

impl Igtk {
    pub fn new(id: u16, ipn: &[u8], igtk: &[u8]) -> Self {
        let mut ipn_buf = [0u8; IGTK_IPN_LEN];
        ipn_buf.copy_from_slice(ipn);
        Self { id, ipn: ipn_buf, igtk: igtk.to_vec() }
    }

    pub fn len(&self) -> usize {
        IGTK_FIXED_LEN + self.igtk.len()
    }
}

pub fn parse(i0: &[u8]) -> IResult<&[u8], Element> {
    // Check whether parsing is finished.
    if i0.len() <= 1 {
        return Ok((&i0[i0.len()..], Element::Padding));
    }

    // Check whether the remaining data is padding.
    let data_len = i0[1];
    if data_len == PADDING_DATA_LEN {
        return parse_padding(&i0[1..]);
    }

    // Read the KDE Header first.
    let (i1, hdr) = try_parse!(i0, call!(parse_header));
    let (i2, bytes) = try_parse!(i1, take!(hdr.data_len()));
    match hdr.oui {
        Oui::DOT11 => match hdr.data_type {
            GTK_DATA_TYPE => {
                let (_, gtk) = try_parse!(bytes, call!(parse_gtk, hdr.data_len()));
                Ok((i2, Element::Gtk(hdr, gtk)))
            }
            IGTK_DATA_TYPE => {
                let (_, gtk) = try_parse!(bytes, call!(parse_igtk, hdr.data_len()));
                Ok((i2, Element::Igtk(hdr, gtk)))
            }
            _ => Ok((i2, Element::UnsupportedKde(hdr))),
        },
        // The WPA1 IE uses the same vendor IE format as a KDE, so we handle it here as a special
        // case.
        Oui::MSFT if hdr.data_type == wpa::VENDOR_SPECIFIC_TYPE => {
            let (_, wpa) = try_parse!(&bytes[..], wpa::from_bytes);
            Ok((i2, Element::LegacyWpa1(wpa)))
        }
        _ => Ok((i2, Element::UnsupportedKde(hdr))),
    }
}

named!(parse_header<&[u8], Header>,
       do_parse!(
            type_: le_u8 >>
            length: le_u8 >>
            oui: take!(3) >>
            data_type: le_u8 >>
           (Header::new(type_, length, oui, data_type))
    )
);

fn parse_padding(input: &[u8]) -> IResult<&[u8], Element> {
    if input.iter().all(|&x| x == 0) {
        Ok((&[], Element::Padding))
    } else {
        // Return ErrorKind::Eof to indicate that we expected that the remaining input should have
        // been all padding bytes.
        Err(nom::Err::Error((input, ErrorKind::Eof)))
    }
}

named_args!(parse_gtk(data_len: usize) <Gtk>,
       do_parse!(
           info: map!(le_u8, GtkInfo) >>
           /* 1 byte reserved */ take!(1) >>
           gtk: take!(data_len - 2) >>
           eof!() >>
           (Gtk{
                info: info,
                gtk: gtk.to_vec(),
           })
    )
);

named_args!(parse_igtk(data_len: usize) <Igtk>,
       do_parse!(
            id: le_u16 >>
            ipn: take!(IGTK_IPN_LEN) >>
            igtk: take!(data_len - IGTK_FIXED_LEN) >>
            eof!() >>
            (Igtk::new(id, ipn, igtk))
    )
);

/// KDE Writer to assist with writing key data elements.
pub struct Writer<A: Appendable> {
    buf: A,
}

impl<A: Appendable> Writer<A> {
    pub fn new(buf: A) -> Self {
        Self { buf }
    }

    pub fn bytes_written(&self) -> usize {
        self.buf.bytes_written()
    }

    pub fn write_protection(&mut self, protection: &ProtectionInfo) -> Result<(), BufferTooSmall> {
        match protection {
            ProtectionInfo::Rsne(rsne) => rsne.write_into(&mut self.buf),
            ProtectionInfo::LegacyWpa(wpa) => write_wpa1_ie(&mut self.buf, wpa),
        }
    }

    fn write_kde_hdr(&mut self, hdr: Header) -> Result<(), BufferTooSmall> {
        if !self.buf.can_append(HDR_LEN) {
            return Err(BufferTooSmall);
        }
        self.buf.append_byte(hdr.type_)?;
        self.buf.append_byte(hdr.len)?;
        self.buf.append_bytes(&hdr.oui[..])?;
        self.buf.append_byte(hdr.data_type)?;
        Ok(())
    }

    pub fn write_gtk(&mut self, gtk_kde: &Gtk) -> Result<(), BufferTooSmall> {
        if !self.buf.can_append(HDR_LEN + gtk_kde.len()) {
            return Err(BufferTooSmall);
        }
        // KDE Header
        let hdr = Header::new_dot11(GTK_DATA_TYPE, gtk_kde.len());
        self.write_kde_hdr(hdr)?;

        // GTK KDE
        self.buf.append_byte(gtk_kde.info.value())?;
        self.buf.append_byte(0)?;
        self.buf.append_bytes(&gtk_kde.gtk[..])?;
        Ok(())
    }

    // We will not use this fn until our authenticator supports MFP.
    #[allow(unused)]
    pub fn write_igtk(&mut self, igtk_kde: &Igtk) -> Result<(), BufferTooSmall> {
        if !self.buf.can_append(HDR_LEN + igtk_kde.len()) {
            return Err(BufferTooSmall);
        }
        // KDE Header
        let hdr = Header::new_dot11(IGTK_DATA_TYPE, igtk_kde.len());
        self.write_kde_hdr(hdr)?;

        // IGTK KDE
        self.buf.append_bytes(&igtk_kde.id.to_le_bytes()[..])?;
        self.buf.append_bytes(&igtk_kde.ipn[..])?;
        self.buf.append_bytes(&igtk_kde.igtk[..])?;
        Ok(())
    }

    pub fn finalize_for_encryption(mut self) -> Result<A, BufferTooSmall> {
        // Optional padding must be added if the key data will be encrypted.
        // See IEEE Std 802.11-2016, 12.7.2 j)
        // Padding is added to extend the key data field to a minimum size of 16 octets or
        // otherwise be a multiple of 8 octets.
        let written = self.bytes_written();
        let padding_len =
            if written < 16 { 16 - written } else { ((written + 7) / 8) * 8 - written };
        if !self.buf.can_append(padding_len) {
            return Err(BufferTooSmall);
        }

        if padding_len != 0 {
            self.buf.append_byte(TYPE)?;
            self.buf.append_bytes_zeroed(padding_len - 1)?;
        }
        Ok(self.buf)
    }

    pub fn finalize_for_plaintext(self) -> Result<A, BufferTooSmall> {
        Ok(self.buf)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::key_data::extract_elements;
    use wlan_common::{assert_variant, test_utils::FixedSizedTestBuffer};

    fn write_and_extract_padding(gtk_len: usize) -> Vec<u8> {
        let mut w = Writer::new(vec![]);
        w.write_gtk(&Gtk::new(2, GtkInfoTx::BothRxTx, &vec![2; gtk_len]))
            .expect("failure writing GTK KDE");
        w.finalize_for_encryption()
            .expect("failure finializing key data")
            .split_off(HDR_LEN + GTK_FIXED_LEN + gtk_len)
    }

    #[test]
    fn test_padding_min_length() {
        let buf = write_and_extract_padding(0);
        assert_eq!(buf, vec![TYPE, 0, 0, 0, 0, 0, 0, 0]);

        let buf = write_and_extract_padding(5);
        assert_eq!(buf, vec![TYPE, 0, 0]);

        let buf = write_and_extract_padding(7);
        assert_eq!(buf, vec![TYPE]);
    }

    #[test]
    fn test_no_padding_expected_for_plaintext() {
        let mut w = Writer::new(vec![]);
        w.write_gtk(&Gtk::new(2, GtkInfoTx::BothRxTx, &vec![2; 2]))
            .expect("failure writing GTK KDE");
        let buf = w.finalize_for_plaintext().expect("failure finalizing key data");
        assert_eq!(
            buf,
            vec![
                // GTK KDE:
                TYPE,
                8,
                0x00,
                0x0F,
                0xAC,
                GTK_DATA_TYPE,
                0b0000_0110,
                0,
                2,
                2
            ]
        );
    }

    #[test]
    fn test_no_padding() {
        let buf = write_and_extract_padding(8);
        assert_eq!(buf, vec![]);
    }

    #[test]
    fn test_padding_multiple_8() {
        let buf = write_and_extract_padding(9);
        assert_eq!(buf, vec![TYPE, 0, 0, 0, 0, 0, 0]);

        let buf = write_and_extract_padding(16);
        assert_eq!(buf, vec![]);
    }

    #[test]
    fn test_write_read_gtk_with_padding() {
        // Write KDE:
        let mut w = Writer::new(vec![]);
        w.write_gtk(&Gtk::new(2, GtkInfoTx::BothRxTx, &vec![24; 5]))
            .expect("failure writing GTK KDE");
        let buf = w.finalize_for_encryption().expect("failure finializing key data");
        #[rustfmt::skip]
        assert_eq!(buf, vec![
            // GTK KDE:
            TYPE, 11, 0x00, 0x0F, 0xAC, GTK_DATA_TYPE, 0b0000_0110, 0, 24, 24, 24, 24, 24,
            // Padding:
            TYPE, 0, 0,
        ]);

        // Read KDE:
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);
        let mut elements = result.unwrap();
        assert_eq!(elements.len(), 2);

        assert_variant!(elements.remove(0), Element::Gtk(hdr, kde) => {
            assert_eq!(hdr, Header { type_: 0xDD, len: 11, oui: Oui::DOT11, data_type: 1 });
            assert_eq!(kde, Gtk { info: GtkInfo(6), gtk: vec![24; 5] });
        });
        assert_variant!(elements.remove(0), Element::Padding);
    }

    #[test]
    fn test_write_read_igtk() {
        let igtk = Igtk::new(10, &[11; 6], &[22; 2]);
        // Write KDE:
        let mut w = Writer::new(vec![]);
        w.write_igtk(&igtk).expect("failure writing IGTK KDE");
        let buf = w.finalize_for_encryption().expect("failure finializing key data");
        #[rustfmt::skip]
        assert_eq!(buf, vec![
            TYPE, 14, 0x00, 0x0F, 0xAC, IGTK_DATA_TYPE, 10, 0, 11,11,11,11,11,11,22,22,
        ]);

        // Read KDE:
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);
        let mut elements = result.unwrap();
        assert_eq!(elements.len(), 1);

        assert_variant!(elements.remove(0), Element::Igtk(hdr, kde) => {
            assert_eq!(hdr, Header {
                type_: 0xDD, len: 14, oui: Oui::DOT11, data_type: IGTK_DATA_TYPE
            });
            assert_eq!(kde, igtk);
        });
    }

    #[test]
    fn test_write_gtk_too_small_buffer() {
        Writer::new(FixedSizedTestBuffer::new(10))
            .write_gtk(&Gtk::new(2, GtkInfoTx::BothRxTx, &vec![24; 5]))
            .expect_err("expected failure writing GTK KDE");
    }

    #[test]
    fn test_write_gtk_sufficient_fixed_buffer() {
        Writer::new(FixedSizedTestBuffer::new(13))
            .write_gtk(&Gtk::new(2, GtkInfoTx::BothRxTx, &vec![24; 5]))
            .expect("expected success writing GTK KDE");
    }

    #[test]
    fn test_create_gtk_element() {
        let gtk = Gtk::new(1, GtkInfoTx::_OnlyRx, &vec![24; 16][..]);
        assert_eq!(gtk.info.key_id(), 1);
        assert_eq!(gtk.info.tx(), 0);
        assert_eq!(&gtk.gtk[..], &vec![24; 16][..]);
    }

    #[test]
    fn test_gtk_len() {
        let gtk_kde = Gtk { info: GtkInfo(0), gtk: vec![] };
        assert_eq!(gtk_kde.len(), 2);

        let gtk_kde = Gtk { info: GtkInfo(0), gtk: vec![0; 16] };
        assert_eq!(gtk_kde.len(), 18);

        let gtk_kde = Gtk { info: GtkInfo(0), gtk: vec![0; 8] };
        assert_eq!(gtk_kde.len(), 10);

        let gtk_kde = Gtk { info: GtkInfo(0), gtk: vec![0; 4] };
        assert_eq!(gtk_kde.len(), 6);

        let gtk_kde = Gtk { info: GtkInfo(0), gtk: vec![0; 32] };
        assert_eq!(gtk_kde.len(), 34);
    }
}
