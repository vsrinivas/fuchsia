// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_cobalt::CobaltEvent,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    fuchsia_cobalt::CobaltSender,
    fuchsia_zircon::DurationNum,
    futures::channel::mpsc,
    ieee80211::Ssid,
    wlan_common::{
        bss::Protection as BssProtection,
        channel::{Cbw, Channel},
    },
    wlan_sme::client::info::{
        DisconnectCause, DisconnectInfo, DisconnectMlmeEventName, DisconnectSource,
    },
};

pub trait CobaltExt {
    fn metric_id(&self) -> u32;
    fn event_codes(&self) -> &[u32];
    fn respond(self, status: fidl_fuchsia_metrics::Status);
}

impl CobaltExt for fidl_fuchsia_metrics::MetricEventLoggerRequest {
    fn metric_id(&self) -> u32 {
        match self {
            Self::LogOccurrence { metric_id, .. } => *metric_id,
            Self::LogInteger { metric_id, .. } => *metric_id,
            Self::LogIntegerHistogram { metric_id, .. } => *metric_id,
            Self::LogString { metric_id, .. } => *metric_id,
            Self::LogMetricEvents { events, .. } => {
                assert_eq!(
                    events.len(),
                    1,
                    "metric_id() can only be called when there's one event"
                );
                events[0].metric_id
            }
            Self::LogCustomEvent { metric_id, .. } => *metric_id,
        }
    }

    fn event_codes(&self) -> &[u32] {
        match self {
            Self::LogOccurrence { event_codes, .. } => &event_codes[..],
            Self::LogInteger { event_codes, .. } => &event_codes[..],
            Self::LogIntegerHistogram { event_codes, .. } => &event_codes[..],
            Self::LogString { event_codes, .. } => &event_codes[..],
            Self::LogMetricEvents { events, .. } => {
                assert_eq!(
                    events.len(),
                    1,
                    "event_codes() can only be called when there's one event"
                );
                &events[0].event_codes[..]
            }
            Self::LogCustomEvent { .. } => {
                panic!("LogCustomEvent has no event codes");
            }
        }
    }

    fn respond(self, status: fidl_fuchsia_metrics::Status) {
        let result = match self {
            Self::LogOccurrence { responder, .. } => responder.send(status),
            Self::LogInteger { responder, .. } => responder.send(status),
            Self::LogIntegerHistogram { responder, .. } => responder.send(status),
            Self::LogString { responder, .. } => responder.send(status),
            Self::LogMetricEvents { responder, .. } => responder.send(status),
            Self::LogCustomEvent { responder, .. } => responder.send(status),
        };
        assert!(result.is_ok());
    }
}

pub fn fake_cobalt_sender() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
    const BUFFER_SIZE: usize = 100;
    let (sender, receiver) = mpsc::channel(BUFFER_SIZE);
    (CobaltSender::new(sender), receiver)
}

pub fn fake_disconnect_info(bssid: [u8; 6]) -> DisconnectInfo {
    DisconnectInfo {
        connected_duration: 30.seconds(),
        bssid,
        ssid: Ssid::from("foo"),
        wsc: None,
        protection: BssProtection::Open,
        channel: Channel { primary: 1, cbw: Cbw::Cbw20 },
        last_rssi: -90,
        last_snr: 1,
        disconnect_source: DisconnectSource::Mlme(DisconnectCause {
            reason_code: fidl_ieee80211::ReasonCode::NoMoreStas,
            mlme_event_name: DisconnectMlmeEventName::DeauthenticateIndication,
        }),
        time_since_channel_switch: None,
    }
}
