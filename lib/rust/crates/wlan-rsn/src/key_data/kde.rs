// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Element;
use bytes::{BufMut, BytesMut};
use failure;
use nom::IResult::{Done, Incomplete};
use nom::{le_u8, IResult, Needed};
use Error;

pub const OUI: [u8; 3] = [0x00, 0x0F, 0xAC];
pub const TYPE: u8 = 0xDD;
const PADDING_DATA_LEN: u8 = 0;
const HDR_LEN: usize = 6;
const GTK_DATA_TYPE: u8 = 1;

// IEEE Std 802.11-2016, 12.7.2, Figure 12-34
#[derive(Default, Debug)]
pub struct Header {
    pub type_: u8,
    pub len: u8,
    pub oui: [u8; 3],
    pub data_type: u8,
}

impl Header {
    pub fn new(type_: u8, len: u8, oui: &[u8], data_type: u8) -> Header {
        let mut hdr = Header {
            type_,
            len,
            data_type,
            ..Default::default()
        };
        hdr.oui.copy_from_slice(oui);
        hdr
    }

    fn data_len(&self) -> usize {
        if self.len < 4 {
            0
        } else {
            (self.len as usize) - 4
        }
    }

    pub fn as_bytes(&self, buf: &mut Vec<u8>) {
        buf.reserve(HDR_LEN);
        buf.put_u8(self.type_);
        buf.put_u8(self.len);
        buf.put_slice(&self.oui[..]);
        buf.put_u8(self.data_type);
    }
}

// IEEE Std 802.11-2016, 12.7.2, j)
pub enum GtkInfoTx {
    OnlyRx = 0,
    BothRxTx = 1,
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
bitfield! {
    pub struct GtkInfo(u8);
    impl Debug;
    pub key_id, set_key_id: 2, 0;
    pub tx, set_tx: 3, 2;
    // Bit 3-7 reserved.
    pub value, _: 7,0;
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
#[derive(Debug)]
pub struct Gtk {
    pub info: GtkInfo,
    // 1 byte reserved.
    pub gtk: Vec<u8>,
}

impl Gtk {
    pub fn new(key_id: u8, tx: GtkInfoTx, gtk: &[u8]) -> Element {
        let mut gtk_info = GtkInfo(0);
        gtk_info.set_key_id(key_id);
        gtk_info.set_tx(tx as u8);
        let gtk = Gtk {
            info: gtk_info,
            gtk: gtk.to_vec(),
        };

        let mut hdr = Header {
            type_: TYPE,
            data_type: GTK_DATA_TYPE,
            ..Default::default()
        };
        hdr.oui.copy_from_slice(&OUI[..]);
        hdr.len = (gtk.len() + 4) as u8;

        Element::Gtk(hdr, gtk)
    }

    pub fn len(&self) -> usize {
        self.gtk.len() + 2
    }

    pub fn as_bytes(&self, buf: &mut Vec<u8>) {
        buf.reserve(2 + self.gtk.len());
        buf.put_u8(self.info.value());
        buf.put_u8(0);
        buf.put_slice(&self.gtk[..]);
    }
}

pub fn parse<'a>(i0: &'a [u8]) -> IResult<&'a [u8], Element> {
    // Check whether parsing is finished.
    if i0.len() <= 1 {
        return Done(&i0[i0.len()..], Element::Padding);
    }

    // Check whether the remaining data is padding.
    let data_len = i0[1];
    if data_len == PADDING_DATA_LEN {
        return parse_padding(&i0[1..]);
    }

    // Read the KDE Header first.
    let (i1, hdr) = try_parse!(i0, call!(parse_header));
    let (i2, bytes) = try_parse!(i1, take!(hdr.data_len()));
    if hdr.oui != OUI {
        return Done(i2, Element::UnsupportedKde(hdr));
    }

    // Once the header was validated, read the KDE data.
    match hdr.data_type {
        GTK_DATA_TYPE => {
            let (_, gtk) = try_parse!(bytes, call!(parse_gtk, hdr.data_len()));
            Done(i2, Element::Gtk(hdr, gtk))
        }
        _ => Done(i2, Element::UnsupportedKde(hdr)),
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

fn parse_padding<'a>(input: &'a [u8]) -> IResult<&'a [u8], Element> {
    for i in 0..input.len() {
        if input[i] != 0 {
            // This should return an error but nom's many0 does only fail when the inner parser
            // returns Incomplete.
            return Incomplete(Needed::Size(input.len()));
        }
    }
    Done(&input[input.len()..], Element::Padding)
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hdr_as_bytes() {
        let mut buf = Vec::with_capacity(256);
        let kde_hdr = Header::new(0x11, 0x12, &vec![0x13, 0x14, 0x15][..], 0xAC);
        kde_hdr.as_bytes(&mut buf);

        let mut expected: Vec<u8> = vec![0x11, 0x12, 0x13, 0x14, 0x15, 0xAC];
        assert_eq!(&buf[..], &expected[..]);
    }

    #[test]
    fn test_create_gtk_element() {
        let gtk_ele = Gtk::new(1, GtkInfoTx::OnlyRx, &vec![24; 16][..]);
        match gtk_ele {
            Element::Gtk(hdr, gtk) => {
                assert_eq!(hdr.type_, TYPE);
                assert_eq!(hdr.len, 22);
                assert_eq!(&hdr.oui[..], &OUI[..]);
                assert_eq!(hdr.data_type, GTK_DATA_TYPE);
                assert_eq!(gtk.info.key_id(), 1);
                assert_eq!(gtk.info.tx(), 0);
                assert_eq!(&gtk.gtk[..], &vec![24; 16][..]);
            }
            _ => assert!(false),
        }
    }

    #[test]
    fn test_gtk_len() {
        let gtk_kde = Gtk {
            info: GtkInfo(0),
            gtk: vec![],
        };
        assert_eq!(gtk_kde.len(), 2);

        let gtk_kde = Gtk {
            info: GtkInfo(0),
            gtk: vec![0; 16],
        };
        assert_eq!(gtk_kde.len(), 18);

        let gtk_kde = Gtk {
            info: GtkInfo(0),
            gtk: vec![0; 8],
        };
        assert_eq!(gtk_kde.len(), 10);

        let gtk_kde = Gtk {
            info: GtkInfo(0),
            gtk: vec![0; 4],
        };
        assert_eq!(gtk_kde.len(), 6);

        let gtk_kde = Gtk {
            info: GtkInfo(0),
            gtk: vec![0; 32],
        };
        assert_eq!(gtk_kde.len(), 34);
    }

    #[test]
    fn test_gtk_as_bytes() {
        let mut buf = Vec::with_capacity(256);
        let gtk_kde = Gtk {
            info: GtkInfo(3),
            gtk: vec![42; 32],
        };
        gtk_kde.as_bytes(&mut buf);

        let mut expected: Vec<u8> = vec![0x03, 0x00];
        expected.append(&mut vec![42; 32]);
        assert_eq!(&buf[..], &expected[..]);
    }

    #[test]
    fn test_gtk_as_bytes_too_short() {
        let mut buf = Vec::with_capacity(32);
        let gtk_kde = Gtk {
            info: GtkInfo(3),
            gtk: vec![42; 32],
        };
        gtk_kde.as_bytes(&mut buf);
    }

}
