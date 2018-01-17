// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate byteorder;
extern crate bytes;

use self::byteorder::{ReadBytesExt, LittleEndian};
use self::bytes::Buf;
use std::io::{Cursor, Read};
use std::io;
use std::result;

use suite_selector;
use akm::Akm;
use cipher::Cipher;
use pmkid::Pmkid;

macro_rules! return_ok_on_empty {
    ( $rdr:expr,$result:expr ) => {
        {
            if $rdr.remaining() == 0 {
                return Ok($result)
            }
        }
    };
}

#[derive(Debug, Fail)]
pub enum Error {
    // When parsing an RSNE all reads are guarded by sufficient length checks. However, read(...)
    // could throw arbitrary errors which we want to catch and not ignore or panic on. This variant
    // will wrap these errors.
    #[fail(display = "unexpected io error while parsing RSNE: {}", _0)]
    UnexpectedIoError(#[cause] io::Error),
    #[fail(display = "invalid RSNE; too short")]
    TooShort,
    #[fail(display = "invalid RSNE; too long")]
    TooLong,
    #[fail(display = "invalid RSNE; expected suite selector (pairwise or AKM) but was too short")]
    ExpectedSuiteSelector,
    #[fail(display = "invalid RSNE; expected pairwise cipher suite list count but failed with: {}", _0)]
    ExpectedPairwiseListCount(#[cause] io::Error),
    #[fail(display = "invalid RSNE; expected AKM suite list count but failed with: {}", _0)]
    ExpectedAkmListCount(#[cause] io::Error),
    #[fail(display = "invalid RSNE; expected PMKID list count but failed with: {}", _0)]
    ExpectedPmkidListCount(#[cause] io::Error),
    #[fail(display = "invalid RSNE; expected PMKID but was too short")]
    ExpectedPmkid,
    #[fail(display = "invalid RSNE; expected RSN capabilities but failed with: {}", _0)]
    ExpectedCapabilities(#[cause] io::Error),
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::UnexpectedIoError(e)
    }
}

pub type Result<T> = result::Result<T, Error>;

// IEEE 802.11-2016, 9.4.2.25.1
#[derive(Default, Debug)]
pub struct Rsne {
    element_id: u8,
    length: u8,
    version: u16,
    group_data_cipher_suite: Option<Cipher>,
    pairwise_cipher_suites: Vec<Cipher>,
    akm_suites: Vec<Akm>,
    rsn_capabilities: u16,
    pmkids: Vec<Pmkid>,
    group_mgmt_cipher_suite: Option<Cipher>,
}

// TODO(hahnr): Figure out whether RSNEs with reserved cipher suites should get rejected.
pub fn from_bytes(data: &[u8]) -> Result<Rsne> {
    let mut rdr = Cursor::new(data);
    if rdr.remaining() > 251 {
        return Err(Error::TooLong);
    }
    if rdr.remaining() < 4 {
        return Err(Error::TooShort);
    }

    let mut rsne = Rsne { ..Default::default() };
    rsne.element_id = rdr.read_u8()?;
    rsne.length = rdr.read_u8()?;
    rsne.version = rdr.read_u16::<LittleEndian>()?;

    // Read group data cipher suite.
    if rdr.remaining() == 0 {
        return Ok(rsne);
    }
    rsne.group_data_cipher_suite = Some(read_suite_selector::<Cipher>(&mut rdr)?);

    // Read pairwise cipher suites.
    return_ok_on_empty!(rdr, rsne);
    let count = rdr.read_u16::<LittleEndian>().map_err(Error::ExpectedPairwiseListCount)?;
    for _ in 0..count {
        rsne.pairwise_cipher_suites.push(read_suite_selector::<Cipher>(&mut rdr)?);
    }

    // Read AKM suites.
    return_ok_on_empty!(rdr, rsne);
    let count = rdr.read_u16::<LittleEndian>().map_err(Error::ExpectedAkmListCount)?;
    for _ in 0..count {
        rsne.akm_suites.push(read_suite_selector::<Akm>(&mut rdr)?)
    }

    // Read RSN capabilities.
    return_ok_on_empty!(rdr, rsne);
    let caps = rdr.read_u16::<LittleEndian>().map_err(Error::ExpectedCapabilities)?;
    rsne.rsn_capabilities = caps;

    // Read PMKIDs.
    return_ok_on_empty!(rdr, rsne);
    let count = rdr.read_u16::<LittleEndian>().map_err(Error::ExpectedPmkidListCount)?;
    for _ in 0..count {
        rsne.pmkids.push(read_pmkid(&mut rdr)?);
    }

    // Read group mgmt cipher suite.
    return_ok_on_empty!(rdr, rsne);
    rsne.group_mgmt_cipher_suite = Some(read_suite_selector::<Cipher>(&mut rdr)?);

    Ok(rsne)
}

fn read_suite_selector<T>(rdr: &mut Cursor<&[u8]>) -> Result<T>
    where T: suite_selector::Factory<Suite=T> {
    if rdr.remaining() < 4 {
        Err(Error::ExpectedSuiteSelector)
    } else {
        let mut oui = [0; 3];
        rdr.read_exact(&mut oui)?;
        let suite_type = rdr.read_u8()?;
        Ok(T::new(oui, suite_type))
    }
}

fn read_pmkid(rdr: &mut Cursor<&[u8]>) -> Result<Pmkid> {
    if rdr.remaining() < 16 {
        Err(Error::ExpectedPmkid)
    } else {
        let mut pmkid = [0; 16];
        rdr.read_exact(&mut pmkid)?;
        Ok(pmkid)
    }
}
