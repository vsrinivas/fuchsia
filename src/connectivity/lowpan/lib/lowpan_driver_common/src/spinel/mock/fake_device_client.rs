// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use assert_matches::assert_matches;
use fidl_fuchsia_lowpan_spinel::DeviceEvent as SpinelDeviceEvent;
use futures::channel::mpsc;
use futures::future::BoxFuture;
use parking_lot::Mutex;
use spinel_pack::EUI64;
use std::collections::HashMap;
use std::sync::Arc;

const INBOUND_WINDOW_SIZE: u32 = 2;

pub const PROP_DEBUG_LOGGING_TEST: Prop = Prop::Unknown(2097151);
pub const PROP_DEBUG_SAVED_PANID_TEST: Prop = Prop::Unknown(2097152);

const NETWORK_DATA_PROPS: &[Prop] =
    &[Prop::Thread(PropThread::OnMeshNets), Prop::Thread(PropThread::OffMeshRoutes)];

#[derive(Debug)]
struct FakeSpinelDevice {
    properties: Arc<Mutex<HashMap<Prop, Vec<u8>>>>,
    saved_network: Arc<Mutex<HashMap<Prop, Vec<u8>>>>,
}

impl Default for FakeSpinelDevice {
    fn default() -> Self {
        let mut properties = HashMap::new();

        properties.insert(Prop::Net(PropNet::Saved), vec![0x00]);

        let ret = FakeSpinelDevice {
            properties: Arc::new(Mutex::new(properties)),
            saved_network: Default::default(),
        };

        ret.on_reset();
        ret
    }
}

impl FakeSpinelDevice {
    fn on_reset(&self) {
        let mut properties = self.properties.lock();
        properties.insert(Prop::Net(PropNet::InterfaceUp), vec![0x00]);
        properties.insert(Prop::Net(PropNet::StackUp), vec![0x00]);
        properties.insert(Prop::Net(PropNet::Role), vec![0x00]);
        properties.insert(Prop::Net(PropNet::NetworkName), vec![0]);
        properties.insert(Prop::Net(PropNet::MasterKey), vec![]);
        properties.insert(Prop::Net(PropNet::Xpanid), vec![0, 0, 0, 0, 0, 0, 0, 0]);
        properties.insert(Prop::Mac(PropMac::Panid), vec![0, 0]);
        properties.insert(Prop::Phy(PropPhy::Chan), vec![11]);
        properties.insert(Prop::Phy(PropPhy::TxPower), vec![0]);
        properties.insert(Prop::Phy(PropPhy::Enabled), vec![0]);
        properties.insert(Prop::Phy(PropPhy::ChanPreferred), vec![11, 12, 13]);
        properties.insert(Prop::Phy(PropPhy::ChanSupported), vec![11, 12, 13]);
        properties.insert(Prop::Mac(PropMac::ScanMask), vec![11, 12, 13]);
        properties.insert(Prop::Mac(PropMac::ScanPeriod), vec![100]);
        properties.insert(Prop::Mac(PropMac::ScanState), vec![0]);
        properties.insert(Prop::Thread(PropThread::AllowLocalNetDataChange), vec![0]);
        properties.insert(
            Prop::Mac(PropMac::LongAddr),
            vec![0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01],
        );
        properties.insert(Prop::Mac(PropMac::ShortAddr), vec![0, 0]);
        properties.insert(Prop::Unknown(2048), vec![]); // SPINEL_PROP_RCP_MAC_KEY
        properties.insert(Prop::Unknown(2049), vec![0, 0, 0, 0]); // SPINEL_PROP_RCP_MAC_FRAME_COUNTER
        properties.insert(Prop::Unknown(2050), vec![0, 0, 0, 0, 0, 0, 0, 0]); // SPINEL_PROP_RCP_TIMESTAMP
        properties.insert(Prop::Unknown(2051), vec![0, 0, 0, 0]); // SPINEL_PROP_RCP_ENH_ACK_PROBING
        properties.insert(Prop::Mac(PropMac::Unknown(4868)), vec![]);
        properties.insert(Prop::Mac(PropMac::Unknown(4869)), vec![]);

        properties.insert(Prop::Stream(PropStream::Net), vec![]);
        properties.insert(Prop::Stream(PropStream::NetInsecure), vec![]);
        properties.insert(Prop::Stream(PropStream::Raw), vec![]);
        properties.insert(Prop::Thread(PropThread::OnMeshNets), vec![]);
        properties.insert(Prop::Thread(PropThread::OffMeshRoutes), vec![]);
        properties.insert(
            Prop::Ipv6(PropIpv6::AddressTable),
            vec![
                0x19, 0x00, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x01, 0x40, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x19,
                0x00, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x01, 0x40, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            ],
        );
        properties.insert(
            Prop::Ipv6(PropIpv6::LlAddr),
            vec![
                0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x01,
            ],
        );
        properties.insert(
            Prop::Ipv6(PropIpv6::MlAddr),
            vec![
                0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x01,
            ],
        );
    }

    // Must send out updates for all network identifying properties after calling this
    fn handle_net_recall(&self) {
        let mut properties = self.properties.lock();
        let saved_network = self.saved_network.lock();
        properties.extend(saved_network.clone().into_iter());
    }

    // Must send out update to PropNet::Saved after calling this
    fn handle_net_save(&self) {
        let mut properties = self.properties.lock();
        let mut saved_network = self.saved_network.lock();
        saved_network.insert(
            Prop::Net(PropNet::NetworkName),
            properties.get(&Prop::Net(PropNet::NetworkName)).unwrap().clone(),
        );
        saved_network.insert(
            Prop::Net(PropNet::Xpanid),
            properties.get(&Prop::Net(PropNet::Xpanid)).unwrap().clone(),
        );
        saved_network.insert(
            Prop::Mac(PropMac::Panid),
            properties.get(&Prop::Mac(PropMac::Panid)).unwrap().clone(),
        );
        saved_network.insert(
            Prop::Phy(PropPhy::Chan),
            properties.get(&Prop::Phy(PropPhy::Chan)).unwrap().clone(),
        );
        saved_network.insert(
            Prop::Net(PropNet::MasterKey),
            properties.get(&Prop::Net(PropNet::MasterKey)).unwrap().clone(),
        );
        properties.insert(Prop::Net(PropNet::Saved), vec![0x01]);
    }

    fn handle_net_clear(&self) {
        let mut properties = self.properties.lock();
        let mut saved_network = self.saved_network.lock();
        saved_network.clear();
        properties.insert(Prop::Net(PropNet::Saved), vec![0x00]);
    }

    fn handle_insert_prop(
        &self,
        frame: SpinelFrameRef<'_>,
        prop: Prop,
        new_value: &[u8],
    ) -> Option<Vec<Vec<u8>>> {
        use std::collections::HashSet;
        let mut response: Vec<u8> = vec![];

        let mut properties = self.properties.lock();
        if NETWORK_DATA_PROPS.contains(&prop)
            && properties.get(&Prop::Thread(PropThread::AllowLocalNetDataChange)).unwrap() != &[1u8]
        {
            spinel_write!(
                &mut response,
                "Ciii",
                frame.header,
                Cmd::PropValueIs,
                Prop::LastStatus,
                Status::InvalidState
            )
            .unwrap();
        } else if let Some(value) = properties.get(&prop) {
            // Dumb insert.
            let mut value =
                HashSet::<Vec<u8>>::try_unpack_from_slice(&value).expect("bad list encoding");
            value.insert(new_value.to_vec());
            properties.insert(prop, value.try_packed().unwrap());
            spinel_write!(
                &mut response,
                "CiiD",
                frame.header,
                Cmd::PropValueIs,
                prop,
                properties.get(&prop).unwrap().as_slice()
            )
            .unwrap();
        } else {
            spinel_write!(
                &mut response,
                "Ciii",
                frame.header,
                Cmd::PropValueIs,
                Prop::LastStatus,
                Status::PropNotFound
            )
            .unwrap();
        }
        assert!(!response.is_empty());
        Some(vec![response])
    }

    fn handle_remove_prop(
        &self,
        frame: SpinelFrameRef<'_>,
        prop: Prop,
        new_value: &[u8],
    ) -> Option<Vec<Vec<u8>>> {
        let mut response: Vec<u8> = vec![];

        let mut properties = self.properties.lock();
        if let Some(value) = properties.get(&prop) {
            // Dumb remove.
            let value = Vec::<Vec<u8>>::try_unpack_from_slice(&value)
                .expect(&format!("bad list encoding for {:?}: {:?}", prop, &value))
                .into_iter()
                .filter(|x| !x.starts_with(new_value))
                .collect::<Vec<Vec<u8>>>()
                .try_packed()
                .unwrap();
            properties.insert(prop, value);

            spinel_write!(
                &mut response,
                "CiiD",
                frame.header,
                Cmd::PropValueIs,
                prop,
                properties.get(&prop).unwrap().as_slice()
            )
            .unwrap();
        } else {
            spinel_write!(
                &mut response,
                "Ciii",
                frame.header,
                Cmd::PropValueIs,
                Prop::LastStatus,
                Status::PropNotFound
            )
            .unwrap();
        }
        assert!(!response.is_empty());
        Some(vec![response])
    }

    fn handle_set_prop(
        &self,
        frame: SpinelFrameRef<'_>,
        prop: Prop,
        new_value: &[u8],
    ) -> Option<Vec<Vec<u8>>> {
        let mut response: Vec<u8> = vec![];
        match prop {
            PROP_DEBUG_SAVED_PANID_TEST => {
                let _panid = u16::try_unpack_from_slice(new_value)
                    .expect("PROP_DEBUG_SAVED_PANID_TEST: Bad format");

                let mut saved_network = self.saved_network.lock();
                saved_network.insert(Prop::Mac(PropMac::Panid), new_value.to_vec());

                spinel_write!(
                    &mut response,
                    "Ciii",
                    frame.header,
                    Cmd::PropValueIs,
                    Prop::LastStatus,
                    Status::Ok
                )
                .unwrap();
            }

            PROP_DEBUG_LOGGING_TEST => {
                let mut full_response: Vec<Vec<u8>> = vec![];
                full_response.push({
                    let mut response: Vec<u8> = vec![];
                    spinel_write!(
                        &mut response,
                        "CiiU",
                        0x80,
                        Cmd::PropValueIs,
                        Prop::Stream(PropStream::Debug),
                        "Testing ",
                    )
                    .unwrap();
                    response
                });

                full_response.push({
                    let mut response: Vec<u8> = vec![];
                    spinel_write!(
                        &mut response,
                        "CiiU",
                        0x80,
                        Cmd::PropValueIs,
                        Prop::Stream(PropStream::Debug),
                        "Debug ",
                    )
                    .unwrap();
                    response
                });

                full_response.push({
                    let mut response: Vec<u8> = vec![];
                    spinel_write!(
                        &mut response,
                        "CiiU",
                        0x80,
                        Cmd::PropValueIs,
                        Prop::Stream(PropStream::Debug),
                        "Output\nAmong other things\n",
                    )
                    .unwrap();
                    response
                });

                full_response.push({
                    let mut response: Vec<u8> = vec![];
                    spinel_write!(&mut response, "Cii", frame.header, Cmd::PropValueIs, prop)
                        .unwrap();
                    response
                });

                return Some(full_response);
            }

            Prop::Mac(PropMac::ScanState) => {
                match ScanState::try_unpack_from_slice(new_value).ok()? {
                    ScanState::Energy => {
                        let mut full_response: Vec<Vec<u8>> = vec![];
                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "Ciii",
                                frame.header,
                                Cmd::PropValueIs,
                                prop,
                                ScanState::Energy
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiCc",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::EnergyScanResult),
                                11,
                                -70
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiCc",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::EnergyScanResult),
                                12,
                                -60
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiCc",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::EnergyScanResult),
                                13,
                                -50
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "Ciii",
                                0x80,
                                Cmd::PropValueIs,
                                prop,
                                ScanState::Idle
                            )
                            .unwrap();
                            response
                        });
                        return Some(full_response);
                    }
                    ScanState::Beacon => {
                        let mut full_response: Vec<Vec<u8>> = vec![];
                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "Ciii",
                                frame.header,
                                Cmd::PropValueIs,
                                prop,
                                ScanState::Beacon
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiD",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::ScanBeacon),
                                NetScanResult {
                                    channel: 11,
                                    rssi: -60,
                                    mac: NetScanResultMac {
                                        long_addr: EUI64([
                                            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
                                        ]),
                                        short_addr: 0xfffe,
                                        panid: 0xaaaa,
                                        lqi: 127,
                                    },
                                    net: NetScanResultNet {
                                        net_type: 2,
                                        flags: 0,
                                        network_name: b"net_chan_11".to_vec(),
                                        xpanid: vec![
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                        ],
                                        steering_data: vec![],
                                    }
                                },
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiD",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::ScanBeacon),
                                NetScanResult {
                                    channel: 12,
                                    rssi: -70,
                                    mac: NetScanResultMac {
                                        long_addr: EUI64([
                                            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
                                        ]),
                                        short_addr: 0xfffe,
                                        panid: 0xaaaa,
                                        lqi: 127,
                                    },
                                    net: NetScanResultNet {
                                        net_type: 2,
                                        flags: 0,
                                        network_name: b"net_chan_12".to_vec(),
                                        xpanid: vec![
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
                                        ],
                                        steering_data: vec![],
                                    }
                                },
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "CiiD",
                                0x80,
                                Cmd::PropValueInserted,
                                Prop::Mac(PropMac::ScanBeacon),
                                NetScanResult {
                                    channel: 13,
                                    rssi: -65,
                                    mac: NetScanResultMac {
                                        long_addr: EUI64([
                                            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
                                        ]),
                                        short_addr: 0xfffe,
                                        panid: 0xaaaa,
                                        lqi: 127,
                                    },
                                    net: NetScanResultNet {
                                        net_type: 2,
                                        flags: 0,
                                        network_name: b"net_chan_13".to_vec(),
                                        xpanid: vec![
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
                                        ],
                                        steering_data: vec![],
                                    }
                                },
                            )
                            .unwrap();
                            response
                        });

                        full_response.push({
                            let mut response: Vec<u8> = vec![];
                            spinel_write!(
                                &mut response,
                                "Ciii",
                                0x80,
                                Cmd::PropValueIs,
                                prop,
                                ScanState::Idle
                            )
                            .unwrap();
                            response
                        });
                        return Some(full_response);
                    }
                    _ => {
                        spinel_write!(
                            &mut response,
                            "Ciii",
                            frame.header,
                            Cmd::PropValueIs,
                            Prop::LastStatus,
                            Status::Unimplemented
                        )
                        .unwrap();
                    }
                }
            }
            Prop::Cntr(PropCntr::AllMacCounters) => {
                spinel_write!(
                    &mut response,
                    "Ciidd",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                )
                .unwrap();
            }
            prop => {
                let mut properties = self.properties.lock();
                if prop == Prop::Net(PropNet::NetworkName) && new_value.last() != Some(&0) {
                    spinel_write!(
                        &mut response,
                        "Ciii",
                        frame.header,
                        Cmd::PropValueIs,
                        Prop::LastStatus,
                        Status::ParseError
                    )
                    .unwrap();
                } else if NETWORK_DATA_PROPS.contains(&prop)
                    && properties.get(&Prop::Thread(PropThread::AllowLocalNetDataChange)).unwrap()
                        != &[1u8]
                {
                    spinel_write!(
                        &mut response,
                        "Ciii",
                        frame.header,
                        Cmd::PropValueIs,
                        Prop::LastStatus,
                        Status::InvalidState
                    )
                    .unwrap();
                } else if prop == Prop::Thread(PropThread::AllowLocalNetDataChange)
                    && new_value != &[0u8]
                    && properties.get(&Prop::Thread(PropThread::AllowLocalNetDataChange)).unwrap()
                        != &[0u8]
                {
                    spinel_write!(
                        &mut response,
                        "Ciii",
                        frame.header,
                        Cmd::PropValueIs,
                        Prop::LastStatus,
                        Status::Already
                    )
                    .unwrap();
                } else {
                    if let Some(value) = properties.get_mut(&prop) {
                        value.clear();
                        value.extend_from_slice(new_value);
                        spinel_write!(
                            &mut response,
                            "CiiD",
                            frame.header,
                            Cmd::PropValueIs,
                            prop,
                            new_value
                        )
                        .unwrap();
                    } else {
                        spinel_write!(
                            &mut response,
                            "Ciii",
                            frame.header,
                            Cmd::PropValueIs,
                            Prop::LastStatus,
                            Status::PropNotFound
                        )
                        .unwrap();
                    }
                }
            }
        }
        assert!(!response.is_empty());
        Some(vec![response])
    }

    fn handle_get_prop(&self, frame: SpinelFrameRef<'_>, prop: Prop) -> Option<Vec<u8>> {
        let mut response: Vec<u8> = vec![];
        match prop {
            Prop::ProtocolVersion => {
                spinel_write!(
                    &mut response,
                    "Ciiii",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    PROTOCOL_MAJOR_VERSION,
                    PROTOCOL_MINOR_VERSION
                )
                .unwrap();
            }
            Prop::NcpVersion => {
                spinel_write!(
                    &mut response,
                    "CiiU",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    "Dummy Device"
                )
                .unwrap();
            }
            Prop::Caps => {
                spinel_write!(
                    &mut response,
                    "Ciiiiiiiiiii",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    CapConfig::Ftd,
                    CapNet::Thread(1, 1),
                    Cap::NetSave,
                    Cap::Counters,
                    Cap::Ot(CapOt::MacRaw),
                    Cap::Config(CapConfig::Radio),
                    Cap::Ot(CapOt::LogMetadata),
                    Cap::Unknown(64),
                    Cap::Ot(CapOt::RadioCoex),
                )
                .unwrap();
            }
            Prop::InterfaceType => {
                spinel_write!(
                    &mut response,
                    "Ciii",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    InterfaceType::Thread
                )
                .unwrap();
            }
            Prop::Unknown(176) => {
                // RCP API Version
                spinel_write!(
                    &mut response,
                    "Ciii",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    openthread_sys::spinel::SPINEL_RCP_API_VERSION
                )
                .unwrap();
            }
            Prop::Phy(PropPhy::Unknown(0x120b)) => {
                spinel_write!(&mut response, "Ciii", frame.header, Cmd::PropValueIs, prop, 0xFFFF)
                    .unwrap();
            }
            Prop::Phy(PropPhy::ChanSupported) => {
                spinel_write!(
                    &mut response,
                    "CiiCCC",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    11,
                    12,
                    13
                )
                .unwrap();
            }
            Prop::HwAddr => {
                spinel_write!(
                    &mut response,
                    "CiiCCCCCCCC",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    0x02,
                    0xAA,
                    0xBB,
                    0xCC,
                    0x11,
                    0x22,
                    0x33,
                    0x44,
                )
                .unwrap();
            }
            Prop::Phy(PropPhy::Rssi) => {
                spinel_write!(&mut response, "Ciic", frame.header, Cmd::PropValueIs, prop, -128,)
                    .unwrap();
            }
            Prop::Net(PropNet::PartitionId) => {
                spinel_write!(
                    &mut response,
                    "CiiL",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    0x00000000,
                )
                .unwrap();
            }
            Prop::Thread(PropThread::Rloc16) => {
                spinel_write!(&mut response, "CiiS", frame.header, Cmd::PropValueIs, prop, 0x0000,)
                    .unwrap();
            }
            Prop::Thread(PropThread::NeighborTable) => {
                spinel_write!(
                    &mut response,
                    "Ciidd",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    NeighborTableEntry {
                        extended_addr: EUI64([0, 1, 2, 3, 4, 5, 6, 7]),
                        short_addr: 0x123,
                        age: 11,
                        is_child: true,
                        link_frame_cnt: 1,
                        mle_frame_cnt: 1,
                        last_rssi: -20,
                        avg_rssi: -20,
                        link_quality: 3,
                        mode: 0x0b,
                    },
                    NeighborTableEntry {
                        extended_addr: EUI64([1, 2, 3, 4, 5, 6, 7, 8]),
                        short_addr: 0x1234,
                        age: 22,
                        is_child: true,
                        link_frame_cnt: 1,
                        mle_frame_cnt: 1,
                        last_rssi: -30,
                        avg_rssi: -30,
                        link_quality: 4,
                        mode: 0x0b,
                    },
                )
                .unwrap();
            }
            Prop::Cntr(PropCntr::AllMacCounters) => {
                spinel_write!(
                    &mut response,
                    "Ciidd",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                    vec![
                        100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
                        115, 116
                    ],
                )
                .unwrap();
            }
            Prop::Phy(PropPhy::RadioCoexMetrics) => {
                spinel_write!(
                    &mut response,
                    "CiiddbL",
                    frame.header,
                    Cmd::PropValueIs,
                    prop,
                    vec![201, 202, 203, 204, 205, 206, 207, 208],
                    vec![301, 302, 303, 304, 305, 306, 307, 308, 309],
                    false,
                    400
                )
                .unwrap();
            }

            prop => {
                let properties = self.properties.lock();
                if let Some(value) = properties.get(&prop) {
                    spinel_write!(
                        &mut response,
                        "CiiD",
                        frame.header,
                        Cmd::PropValueIs,
                        prop,
                        value.as_slice()
                    )
                    .unwrap();
                } else {
                    spinel_write!(
                        &mut response,
                        "Ciii",
                        frame.header,
                        Cmd::PropValueIs,
                        Prop::LastStatus,
                        Status::PropNotFound
                    )
                    .unwrap();
                }
            }
        }
        assert!(!response.is_empty());
        Some(response)
    }

    fn handle_command(&self, frame: &[u8]) -> Vec<Vec<u8>> {
        let mut frame = SpinelFrameRef::try_unpack_from_slice(frame).unwrap();

        if frame.header.tid().is_none() {
            return vec![];
        }

        match frame.cmd {
            Cmd::PropValueGet => {
                let mut payload = frame.payload.iter();
                let prop = Prop::try_unpack(&mut payload);
                if payload.count() == 0 || prop.as_ref().ok() == Some(&Prop::Unknown(2050)) {
                    prop.ok()
                        .and_then(|prop| self.handle_get_prop(frame, prop))
                        .map(|r| vec![r])
                        .unwrap_or(vec![])
                } else {
                    // There was data after the property value,
                    // which isn't allowed on a get, so we
                    // return an error.
                    let mut response: Vec<u8> = vec![];
                    spinel_write!(
                        &mut response,
                        "Ciii",
                        frame.header,
                        Cmd::PropValueIs,
                        Prop::LastStatus,
                        Status::ParseError,
                    )
                    .unwrap();
                    vec![response]
                }
            }
            Cmd::PropValueSet => {
                let mut payload = frame.payload.iter();
                let prop = Prop::try_unpack(&mut payload).ok();
                let value = <&[u8]>::try_unpack(&mut payload).unwrap();
                prop.and_then(|prop| self.handle_set_prop(frame, prop, value)).unwrap_or(vec![])
            }
            Cmd::PropValueInsert => {
                let mut payload = frame.payload.iter();
                let prop = Prop::try_unpack(&mut payload).ok();
                let value = <&[u8]>::try_unpack(&mut payload).unwrap();
                prop.and_then(|prop| self.handle_insert_prop(frame, prop, value)).unwrap_or(vec![])
            }
            Cmd::PropValueRemove => {
                let mut payload = frame.payload.iter();
                let prop = Prop::try_unpack(&mut payload).ok();
                let value = <&[u8]>::try_unpack(&mut payload).unwrap();
                prop.and_then(|prop| self.handle_remove_prop(frame, prop, value)).unwrap_or(vec![])
            }
            Cmd::NetClear => {
                self.handle_net_clear();
                self.handle_get_prop(frame, Prop::Net(PropNet::Saved))
                    .map(|r| vec![r])
                    .unwrap_or(vec![])
            }
            Cmd::NetSave => {
                self.handle_net_save();
                self.handle_get_prop(frame, Prop::Net(PropNet::Saved))
                    .map(|r| vec![r])
                    .unwrap_or(vec![])
            }
            Cmd::NetRecall => {
                self.handle_net_recall();
                let mut ret = self
                    .handle_get_prop(frame, Prop::Net(PropNet::Saved))
                    .map(|r| vec![r])
                    .unwrap_or(vec![]);
                frame.header = Header::new(frame.header.nli(), None).unwrap();
                ret.extend(self.handle_get_prop(frame, Prop::Net(PropNet::NetworkName)));
                ret.extend(self.handle_get_prop(frame, Prop::Net(PropNet::Xpanid)));
                ret.extend(self.handle_get_prop(frame, Prop::Phy(PropPhy::Chan)));
                ret.extend(self.handle_get_prop(frame, Prop::Net(PropNet::MasterKey)));
                ret
            }
            _ => {
                let mut response: Vec<u8> = vec![];
                spinel_write!(
                    &mut response,
                    "Ciii",
                    frame.header,
                    Cmd::PropValueIs,
                    Prop::LastStatus,
                    Status::Unimplemented
                )
                .unwrap();
                vec![response]
            }
        }
    }
}

/// Returns a fake spinel device client and a future
/// to run in the background.
///
/// This is only used for testing.
pub fn new_fake_spinel_pair() -> (
    SpinelDeviceSink<MockDeviceProxy>,
    SpinelDeviceStream<MockDeviceProxy, mpsc::Receiver<Result<SpinelDeviceEvent, fidl::Error>>>,
    BoxFuture<'static, ()>,
) {
    let (device_sink, device_stream, mut device_event_sender, mut device_request_receiver) =
        new_mock_spinel_pair(2000);

    let fake_device = FakeSpinelDevice::default();

    let ncp_task = async move {
        let outbound_frames_remaining = FlowWindow::default();
        let mut inbound_frames_remaining = 0;
        let mut is_open = false;
        let mut did_send_reset = false;

        traceln!("new_fake_spinel_device[ncp_task]: Start.");

        while let Some(request) = device_request_receiver.next().await {
            match request {
                DeviceRequest::Open => {
                    traceln!("new_fake_spinel_device[ncp_task]: Got Open");
                    is_open = true;

                    traceln!(
                        "new_fake_spinel_device[ncp_task]: Sending SpinelDeviceEvent::OnReadyForSendFrames"
                    );
                    let event = SpinelDeviceEvent::OnReadyForSendFrames {
                        number_of_frames: INBOUND_WINDOW_SIZE,
                    };
                    assert_matches!(device_event_sender.start_send(Ok(event)), Ok(_));
                    inbound_frames_remaining = INBOUND_WINDOW_SIZE;
                    did_send_reset = false;
                    outbound_frames_remaining.reset();
                }
                DeviceRequest::Close => {
                    traceln!("new_fake_spinel_device[ncp_task]: Got Close");
                    is_open = false;
                    outbound_frames_remaining.reset();
                }
                DeviceRequest::GetMaxFrameSize => {
                    traceln!("new_fake_spinel_device[ncp_task]: Got GetMaxFrameSize");
                }
                DeviceRequest::SendFrame(frame) => {
                    traceln!("new_fake_spinel_device[ncp_task]: Got SendFrame");
                    assert!(inbound_frames_remaining != 0, "inbound_frames_remaining == 0");
                    inbound_frames_remaining -= 1;

                    if frame == [129u8, 1u8] {
                        traceln!("new_fake_spinel_device[ncp_task]: Got software reset command!");
                        fake_device.on_reset();

                        outbound_frames_remaining.dec(1).await;
                        assert_matches!(
                            device_event_sender.start_send(Ok(SpinelDeviceEvent::OnReceiveFrame {
                                data: vec![0x80, 0x06, 0x00, 113]
                            })),
                            Ok(_)
                        );
                        did_send_reset = true;
                    } else {
                        for response in fake_device.handle_command(frame.as_slice()) {
                            outbound_frames_remaining.dec(1).await;
                            assert_matches!(
                                device_event_sender.start_send(Ok(
                                    SpinelDeviceEvent::OnReceiveFrame { data: response }
                                )),
                                Ok(_)
                            );
                        }
                    }

                    let event = SpinelDeviceEvent::OnReadyForSendFrames { number_of_frames: 1 };
                    assert_matches!(device_event_sender.start_send(Ok(event)), Ok(_));
                    inbound_frames_remaining += 1;
                }
                DeviceRequest::ReadyToReceiveFrames(n) => {
                    traceln!("new_fake_spinel_device[ncp_task]: Got ReadyToReceiveFrames");
                    assert!(is_open);
                    outbound_frames_remaining.inc(n);
                    if !did_send_reset && outbound_frames_remaining.dec(1).now_or_never().is_some()
                    {
                        assert_matches!(
                            device_event_sender.start_send(Ok(SpinelDeviceEvent::OnReceiveFrame {
                                data: vec![0x80, 0x06, 0x00, 113]
                            })),
                            Ok(_)
                        );
                        did_send_reset = true;
                    }
                }
            }
        }
    };

    (device_sink, device_stream, ncp_task.boxed())
}
