// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rsne;
use nom::{le_u8, le_u16, IResult, Needed, ErrorKind};
use nom::IResult::{Done, Incomplete};
use super::Element;

pub const OUI: [u8; 3] = [0x00, 0x0F, 0xAC];
pub const TYPE: u8 = 0xDD;
const PADDING_DATA_LEN: u8 = 0;
const GTK_DATA_TYPE: u8 = 1;

#[derive(Default, Debug)]
pub struct Header {
    pub type_: u8,
    pub len: u8,
    pub oui: [u8; 3],
    pub data_type: u8,
}

impl Header {
    fn new(type_: u8, len: u8, oui: &[u8], data_type: u8) -> Header {
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
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
bitfield! {
    pub struct GtkInfo(u8);
    impl Debug;
    pub key_id, set_key_id: 2, 0;
    pub tx, set_key_type: 3, 2;
    // Bit 3-7 reserved.
    pub value, _: 7,0;
}

// IEEE Std 802.11-2016, 12.7.2, Figure 12-35
#[derive(Debug)]
pub struct Gtk {
    pub info: GtkInfo,
    pub gtk: Vec<u8>,
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