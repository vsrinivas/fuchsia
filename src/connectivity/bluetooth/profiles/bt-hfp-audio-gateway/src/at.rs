// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTICE:
// This module is temporary and its removal is tracked by fxbug.dev/59648.
// Dependencies on this module should be kept to a bare minimum. It is advisable to put the
// following anywhere this module is used: "TODO (fxbug.dev/59648): Replace with at-commands types."

// This entire module is to be replaced by fxbug.dev/59648. In the meantime, allow unused items.
#![allow(unused)]

use crate::procedure::query_operator_selection::NetworkOperatorNameFormat;
use crate::protocol::features::{AgFeatures, HfFeatures};
use regex::Regex;

#[derive(Debug, Default, Clone, Copy, PartialEq)]
pub struct IndicatorStatus {
    pub service: bool,
    pub call: bool,
    pub callsetup: (),
    pub callheld: (),
    pub signal: u8,
    pub roam: bool,
    pub batt: u8,
}

pub fn ag_features_ok(features: AgFeatures) -> Vec<u8> {
    format!("\r\n+BRSF: {}\r\n\r\nOK\r\n", features.bits()).into_bytes()
}

pub fn ag_three_way_support() -> Vec<u8> {
    format!("\r\n+CHLD: (0,1,1X,2,2X,3,4)\r\n\r\nOK\r\n").into_bytes()
}

pub fn ok() -> Vec<u8> {
    b"\r\nOK\r\n".to_vec()
}

pub fn error() -> Vec<u8> {
    b"\r\nERROR\r\n".to_vec()
}

pub fn at_ag_supported_indicators() -> Vec<u8> {
    b"\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0,3)),(\"callheld\",(0,2)),(\"signal\",(0,5)),(\"roam\",(0,1)),(\"battchg\",(0,5))\r\n\r\nOK\r\n".to_vec()
}

pub fn at_hf_ind_ag_sup_resp(safety: bool, battery: bool) -> Vec<u8> {
    let mut supported = String::new();
    if safety {
        supported.push('1');
    }
    if battery {
        if !supported.is_empty() {
            supported.push(',');
        }
        supported.push('2');
    }
    format!("\r\n+BIND: ({})\r\n\r\nOK\r\n", supported).into_bytes()
}

pub fn at_ag_indicator_statuses(status: IndicatorStatus) -> Vec<u8> {
    format!("\r\n+CIND: {service},{call},{callsetup},{callheld},{signal},{roam},{battchg}\r\n\r\nOK\r\n",
        service=status.service as u8,
        call=status.call as u8,
        callsetup=0,
        callheld=0,
        signal=status.signal,
        roam=status.roam as u8,
        battchg=status.batt,
    ).into_bytes()
}

#[derive(Debug, Clone, PartialEq)]
pub enum AtHfMessage {
    // From HF to AG
    HfFeatures(HfFeatures),
    HfCodecSup(Vec<u32>),
    AgIndSupRequest,
    AgIndStat,
    HfIndStatusAgEnable,
    ThreeWaySupport,
    HfIndSup(bool, bool),
    HfIndAgSup,
    CurrentCalls,
    Nrec(bool),
    SetNetworkOperatorFormat(NetworkOperatorNameFormat),
    GetNetworkOperator,
    Unknown(String),
}

#[derive(Debug, Clone)]
pub enum AtAgMessage {
    // From AG to HF
    AgFeatures(AgFeatures),
    AgThreeWaySupport,
    AgIndStat(IndicatorStatus),
    Ok,
    Error,
    AgSupportedIndicators,
    AgSupportedHfSupResp { safety: bool, battery: bool },
    AgNetworkOperatorName(Option<String>),
}

impl AtAgMessage {
    pub fn into_bytes(self) -> Vec<u8> {
        use AtAgMessage::*;
        match self {
            AgFeatures(features) => ag_features_ok(features),
            AgThreeWaySupport => ag_three_way_support(),
            AgIndStat(status) => at_ag_indicator_statuses(status),
            Ok => ok(),
            Error => error(),
            AgSupportedIndicators => at_ag_supported_indicators(),
            AgSupportedHfSupResp { safety, battery } => at_hf_ind_ag_sup_resp(safety, battery),
            AgNetworkOperatorName(name) => unimplemented!(),
        }
    }
}

// Parser stores Regex objects and is instantiated so that Regex parsers are
// only instantiated once per "PeerTask". There is no other state stored in the parser.
// It does not support partial parsing of commands at this time.
pub struct Parser {
    re_hf_features: Regex,
    re_ag_ind_sup_req: Regex,
    re_ag_ind_stat: Regex,
    re_cur_calls: Regex,
    re_codec_sup: Regex,
    re_hf_ind_sup: Regex,
    re_hf_ind_ag_sup: Regex,
    re_hf_ind_status_ag_enable: Regex,
    re_three_way_sup: Regex,
}

impl Default for Parser {
    fn default() -> Self {
        Self {
            re_hf_features: Regex::new(r"\AAT\+BRSF=(?P<features>\d+)\r\z").unwrap(),
            re_ag_ind_sup_req: Regex::new(r"\AAT\+CIND=\?\r\z").unwrap(),
            re_ag_ind_stat: Regex::new(r"\AAT\+CIND\?\r\z").unwrap(),
            re_cur_calls: Regex::new(r"\AAT\+CLCC\r\z").unwrap(),
            re_codec_sup: Regex::new(r"\AAT\+BAC=(?P<codecs>.+)\r\z").unwrap(),
            re_hf_ind_sup: Regex::new(r"\AAT\+BIND=(?P<ind_sup>[\d,]+)\r\z").unwrap(),
            re_hf_ind_ag_sup: Regex::new(r"\AAT\+BIND=\?\r\z").unwrap(),
            re_hf_ind_status_ag_enable: Regex::new(r"\AAT\+CMER\r\z").unwrap(),
            re_three_way_sup: Regex::new(r"\AAT\+CHLD=\?\r\z").unwrap(),
        }
    }
}

impl Parser {
    /// Parse an incoming RFCOMM message as an AT command. Expects a single complete command.
    pub fn parse(&self, bytes: &[u8]) -> AtHfMessage {
        let unicode = std::str::from_utf8(bytes).expect("Valid unicode data");
        assert!(!unicode.is_empty(), "empty bytes passed to parse");
        log::info!("{:?}", unicode);
        if let Some(groups) = self.re_hf_features.captures(unicode) {
            let features: u32 = groups.name("features").unwrap().as_str().parse().unwrap();
            AtHfMessage::HfFeatures(HfFeatures::from_bits(features).unwrap())
        } else if self.re_ag_ind_sup_req.is_match(unicode) {
            AtHfMessage::AgIndSupRequest
        } else if self.re_ag_ind_stat.is_match(unicode) {
            AtHfMessage::AgIndStat
        } else if self.re_cur_calls.is_match(unicode) {
            AtHfMessage::CurrentCalls
        } else if self.re_three_way_sup.is_match(unicode) {
            AtHfMessage::ThreeWaySupport
        } else if let Some(groups) = self.re_codec_sup.captures(unicode) {
            let codecs = groups.name("codecs").unwrap().as_str();
            let codecs = codecs.split(",").map(|s| s.trim().parse().unwrap()).collect();
            AtHfMessage::HfCodecSup(codecs)
        } else if let Some(groups) = self.re_hf_ind_sup.captures(unicode) {
            let supported = groups.name("ind_sup").unwrap().as_str();
            log::info!("ind sup: {:?}", supported);
            let supported = supported
                .split(",")
                .map(|s| {
                    let s = s.trim();
                    log::info!("trimmed: {:?}", s);
                    s.parse().unwrap()
                })
                .collect::<Vec<u32>>();
            AtHfMessage::HfIndSup(supported[0] != 0, supported[1] != 0)
        } else if self.re_hf_ind_ag_sup.is_match(unicode) {
            AtHfMessage::HfIndAgSup
        } else if self.re_hf_ind_status_ag_enable.is_match(unicode) {
            AtHfMessage::HfIndStatusAgEnable
        } else {
            AtHfMessage::Unknown(unicode.to_string())
        }
    }
}
