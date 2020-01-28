// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::{
        ipv4, ipv6, link,
        ppp::{
            FrameError, FrameReceiver, FrameTransmitter, ProtocolError, ProtocolState,
            PROTOCOL_IPV4_CONTROL, PROTOCOL_IPV6_CONTROL, PROTOCOL_LINK_CONTROL,
        },
    },
    fuchsia_async::{self as fasync, futures::future::BoxFuture},
    packet_new::{Buf, ParseBuffer},
    ppp_packet::{
        ipv4::ControlOption as Ipv4ControlOption, ipv6::ControlOption as Ipv6ControlOption,
        link::ControlOption as LinkControlOption, PppPacket,
    },
    std::{
        sync::{Arc, Mutex, Once},
        time::{Duration, Instant},
    },
};

macro_rules! assert_closed {
    ($x:expr) => {
        match $x {
            ProtocolState::Closed(_) => {}
            state => panic!("State was {:?}, expected Closed", state),
        }
    };
}

macro_rules! assert_req_sent {
    ($x:expr) => {
        match $x {
            ProtocolState::RequestSent(_) => {}
            state => panic!("State was {:?}, expected RequestSent", state),
        }
    };
}

macro_rules! assert_ack_sent {
    ($x:expr) => {
        match $x {
            ProtocolState::AckSent(_) => {}
            state => panic!("State was {:?}, expected AckSent", state),
        }
    };
}

macro_rules! assert_ack_received {
    ($x:expr) => {
        match $x {
            ProtocolState::AckReceived(_) => {}
            state => panic!("State was {:?}, expected AckReceived", state),
        }
    };
}

macro_rules! assert_opened {
    ($x:expr) => {
        match $x {
            ProtocolState::Opened(_) => {}
            state => panic!("State was {:?}, expected Opened", state),
        }
    };
}

static START: Once = Once::new();

#[fasync::run_singlethreaded(test)]
async fn test_link_open_ack_received() -> Result<(), anyhow::Error> {
    START.call_once(|| {
        fuchsia_syslog::init().unwrap();
    });

    let now = Instant::now();

    let tx_req = vec![0xc0, 0x21, 0x01, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xe0, 0x1e, 0x87, 0x64];
    let rx_ack = vec![0xc0, 0x21, 0x02, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xe0, 0x1e, 0x87, 0x64];

    let rx_req = vec![0xc0, 0x21, 0x01, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];
    let tx_ack = vec![0xc0, 0x21, 0x02, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];

    let device = TestDevice::new(vec![rx_ack, rx_req], vec![tx_req, tx_ack]);

    let magic_number = 0xe01e_8764;

    let mut link_state = ProtocolState::new(vec![LinkControlOption::MagicNumber(magic_number)]);
    assert_closed!(link_state);
    link_state = link::update(link_state, &device, now).await?;
    assert_req_sent!(link_state);
    link_state = link::update(link_state, &device, now).await?;
    assert_req_sent!(link_state);

    let mut packet = device.rx_frame().await?;
    let mut buf = Buf::new(packet.as_mut_slice(), ..);
    let ppp_packet = buf.parse::<PppPacket<_>>()?;
    assert_eq!(ppp_packet.protocol(), PROTOCOL_LINK_CONTROL);
    link_state = link::receive(link_state, &device, buf, now).await?;
    assert_ack_received!(link_state);

    link_state = link::update(link_state, &device, now).await?;
    assert_ack_received!(link_state);

    let mut packet = device.rx_frame().await?;
    let mut buf = Buf::new(packet.as_mut_slice(), ..);
    let ppp_packet = buf.parse::<PppPacket<_>>()?;
    assert_eq!(ppp_packet.protocol(), PROTOCOL_LINK_CONTROL);
    link_state = link::receive(link_state, &device, buf, now).await?;
    assert_opened!(link_state);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_link_open_ack_sent() -> Result<(), anyhow::Error> {
    START.call_once(|| {
        fuchsia_syslog::init().unwrap();
    });

    let now = Instant::now();

    let tx_req = vec![0xc0, 0x21, 0x01, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xe0, 0x1e, 0x87, 0x64];
    let rx_ack = vec![0xc0, 0x21, 0x02, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xe0, 0x1e, 0x87, 0x64];

    let rx_req = vec![0xc0, 0x21, 0x01, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];
    let tx_ack = vec![0xc0, 0x21, 0x02, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];

    let device = TestDevice::new(vec![rx_req, rx_ack], vec![tx_req, tx_ack]);

    let magic_number = 0xe01e_8764;

    let mut link_state = ProtocolState::new(vec![LinkControlOption::MagicNumber(magic_number)]);
    assert_closed!(link_state);
    link_state = link::update(link_state, &device, now).await?;
    assert_req_sent!(link_state);
    link_state = link::update(link_state, &device, now).await?;
    assert_req_sent!(link_state);

    let mut packet = device.rx_frame().await?;
    let mut buf = Buf::new(packet.as_mut_slice(), ..);
    let ppp_packet = buf.parse::<PppPacket<_>>()?;
    assert_eq!(ppp_packet.protocol(), PROTOCOL_LINK_CONTROL);
    link_state = link::receive(link_state, &device, buf, now).await?;
    assert_ack_sent!(link_state);

    link_state = link::update(link_state, &device, now).await?;
    assert_ack_sent!(link_state);

    let mut packet = device.rx_frame().await?;
    let mut buf = Buf::new(packet.as_mut_slice(), ..);
    let ppp_packet = buf.parse::<PppPacket<_>>()?;
    assert_eq!(ppp_packet.protocol(), PROTOCOL_LINK_CONTROL);
    link_state = link::receive(link_state, &device, buf, now).await?;
    assert_opened!(link_state);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_restart() -> Result<(), anyhow::Error> {
    START.call_once(|| {
        fuchsia_syslog::init().unwrap();
    });

    let now = Instant::now();
    let expired_once = now + Duration::from_secs(3);
    let expired_twice = expired_once + Duration::from_secs(3);

    let tx_req = vec![0xc0, 0x21, 0x01, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xe0, 0x1e, 0x87, 0x64];

    let rx_req = vec![0xc0, 0x21, 0x01, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];
    let tx_ack = vec![0xc0, 0x21, 0x02, 0x01, 0x00, 0x0a, 0x05, 0x06, 0x7d, 0x37, 0xcc, 0x34];
    let device =
        TestDevice::new(vec![rx_req], vec![tx_req.clone(), tx_req.clone(), tx_ack, tx_req]);

    let magic_number = 0xe01e_8764;

    let mut link_state = ProtocolState::new(vec![LinkControlOption::MagicNumber(magic_number)]);
    assert_closed!(link_state);
    link_state = link::update(link_state, &device, now).await?;
    assert_req_sent!(link_state);
    link_state = link::update(link_state, &device, expired_once).await?;
    assert_req_sent!(link_state);

    let mut packet = device.rx_frame().await?;
    let mut buf = Buf::new(packet.as_mut_slice(), ..);
    let ppp_packet = buf.parse::<PppPacket<_>>()?;
    assert_eq!(ppp_packet.protocol(), PROTOCOL_LINK_CONTROL);
    link_state = link::receive(link_state, &device, buf, expired_once).await?;
    assert_ack_sent!(link_state);

    link_state = link::update(link_state, &device, expired_twice).await?;
    assert_ack_sent!(link_state);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_full_ipv4_ipv6() -> Result<(), anyhow::Error> {
    START.call_once(|| {
        fuchsia_syslog::init().unwrap();
    });

    let now = Instant::now();

    let tx_req = vec![0xc0, 0x21, 0x01, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xf6, 0x24, 0xab, 0x38];
    let rx_ack = vec![0xc0, 0x21, 0x02, 0x00, 0x00, 0x0a, 0x05, 0x06, 0xf6, 0x24, 0xab, 0x38];

    let rx_req1 = vec![
        0xc0, 0x21, 0x01, 0x01, 0x00, 0x14, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06, 0x4f,
        0x9c, 0x6b, 0x9f, 0x07, 0x02, 0x08, 0x02,
    ];
    let tx_rej = vec![
        0xc0, 0x21, 0x04, 0x01, 0x00, 0x0e, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00, 0x07, 0x02, 0x08,
        0x02,
    ];
    let rx_req2 = vec![0xc0, 0x21, 0x01, 0x02, 0x00, 0x0a, 0x05, 0x06, 0x4f, 0x9c, 0x6b, 0x9f];
    let tx_ack = vec![0xc0, 0x21, 0x02, 0x02, 0x00, 0x0a, 0x05, 0x06, 0x4f, 0x9c, 0x6b, 0x9f];

    let tx_ip_req = vec![0x80, 0x21, 0x01, 0x00, 0x00, 0x0a, 0x03, 0x06, 0x64, 0x76, 0xee, 0x27];
    let tx_ipv6_req = vec![
        0x80, 0x57, 0x01, 0x00, 0x00, 0x0e, 0x01, 0x0a, 0x64, 0x76, 0xee, 0x27, 0x64, 0x76, 0xee,
        0x27,
    ];

    let rx_echo_req = vec![0xc0, 0x21, 0x09, 0x00, 0x00, 0x08, 0x4f, 0x9c, 0x6b, 0x9f];
    let tx_echo_reply = vec![0xc0, 0x21, 0x0a, 0x00, 0x00, 0x08, 0xf6, 0x24, 0xab, 0x38];

    let rx_comp_req = vec![
        0x80, 0xfd, 0x01, 0x01, 0x00, 0x0f, 0x1a, 0x04, 0x78, 0x00, 0x18, 0x04, 0x78, 0x00, 0x15,
        0x03, 0x2f,
    ];
    let tx_prot_comp_rej = vec![
        0xc0, 0x21, 0x08, 0x01, 0x00, 0x15, 0x80, 0xfd, 0x01, 0x01, 0x00, 0x0f, 0x1a, 0x04, 0x78,
        0x00, 0x18, 0x04, 0x78, 0x00, 0x15, 0x03, 0x2f,
    ];

    let rx_ip_req1 = vec![
        0x80, 0x21, 0x01, 0x01, 0x00, 0x10, 0x02, 0x06, 0x00, 0x2d, 0x0f, 0x01, 0x03, 0x06, 0x64,
        0x77, 0xbf, 0x4b,
    ];
    let tx_ip_rej = vec![0x80, 0x21, 0x04, 0x01, 0x00, 0x0a, 0x02, 0x06, 0x00, 0x2d, 0x0f, 0x01];

    let rx_ipv6_req = vec![
        0x80, 0x57, 0x01, 0x01, 0x00, 0x0e, 0x01, 0x0a, 0xd8, 0x18, 0xc2, 0xcb, 0x2a, 0x35, 0x34,
        0x55,
    ];
    let tx_ipv6_ack = vec![
        0x80, 0x57, 0x02, 0x01, 0x00, 0x0e, 0x01, 0x0a, 0xd8, 0x18, 0xc2, 0xcb, 0x2a, 0x35, 0x34,
        0x55,
    ];

    let rx_ip_ack = vec![0x80, 0x21, 0x02, 0x00, 0x00, 0x0a, 0x03, 0x06, 0x64, 0x76, 0xee, 0x27];
    let rx_ipv6_ack = vec![
        0x80, 0x57, 0x02, 0x00, 0x00, 0x0e, 0x01, 0x0a, 0x64, 0x76, 0xee, 0x27, 0x64, 0x76, 0xee,
        0x27,
    ];

    let rx_ip_req2 = vec![0x80, 0x21, 0x01, 0x02, 0x00, 0x0a, 0x03, 0x06, 0x64, 0x77, 0xbf, 0x4b];
    let tx_ip_ack = vec![0x80, 0x21, 0x02, 0x02, 0x00, 0x0a, 0x03, 0x06, 0x64, 0x77, 0xbf, 0x4b];

    let rx_term_req = vec![
        0xc0, 0x21, 0x05, 0x03, 0x00, 0x10, 0x55, 0x73, 0x65, 0x72, 0x20, 0x72, 0x65, 0x71, 0x75,
        0x65, 0x73, 0x74,
    ];
    let tx_term_ack = vec![0xc0, 0x21, 0x06, 0x03, 0x00, 0x04];

    let device = TestDevice::new(
        vec![
            rx_ack,
            rx_req1,
            rx_req2,
            rx_echo_req,
            rx_comp_req,
            rx_ip_req1,
            rx_ipv6_req,
            rx_ip_ack,
            rx_ipv6_ack,
            rx_ip_req2,
            rx_term_req,
        ],
        vec![
            tx_req,
            tx_rej,
            tx_ack,
            tx_ip_req,
            tx_ipv6_req,
            tx_echo_reply,
            tx_prot_comp_rej,
            tx_ip_rej,
            tx_ipv6_ack,
            tx_ip_ack,
            tx_term_ack,
        ],
    );

    let magic_number = 0xf624ab38;
    let ip_address: u32 = 0x6476_ee27;
    let interface_identifier: u64 = 0x6476_ee27_6476_ee27;

    let mut link_state = ProtocolState::new(vec![LinkControlOption::MagicNumber(magic_number)]);
    let mut ipv4_state = ProtocolState::new(vec![Ipv4ControlOption::IpAddress(ip_address)]);
    let mut ipv6_state =
        ProtocolState::new(vec![Ipv6ControlOption::InterfaceIdentifier(interface_identifier)]);

    let mut reject_identifier = 0;

    loop {
        link_state = link::update(link_state, &device, now).await?;
        ipv4_state = ipv4::update(ipv4_state, &link_state, &device, now).await?;
        ipv6_state = ipv6::update(ipv6_state, &link_state, &device, now).await?;

        let mut packet = device.rx_frame().await?;
        let mut buf = Buf::new(packet.as_mut_slice(), ..);
        let ppp_packet = buf.parse::<PppPacket<_>>()?;
        let protocol = ppp_packet.protocol();

        match protocol {
            PROTOCOL_LINK_CONTROL => match link::receive(link_state, &device, buf, now).await {
                Ok(new_state)
                | Err(ProtocolError::InvalidAck(new_state))
                | Err(ProtocolError::UnacceptableReq(new_state)) => {
                    link_state = new_state;
                }
                Err(ProtocolError::Terminated) => break,
                error => {
                    panic!("Unexpected error when receiving link control packet {:?}", error);
                }
            },
            PROTOCOL_IPV4_CONTROL => {
                if let ProtocolState::Opened(opened) = &link_state {
                    match ipv4::receive(ipv4_state, opened, &device, buf, now).await {
                        Ok(new_state)
                        | Err(ProtocolError::InvalidAck(new_state))
                        | Err(ProtocolError::UnacceptableReq(new_state)) => {
                            ipv4_state = new_state;
                        }
                        error => {
                            panic!(
                                "Unexpected error when receiving IPv4 control packet {:?}",
                                error
                            );
                        }
                    }
                }
            }
            PROTOCOL_IPV6_CONTROL => {
                if let ProtocolState::Opened(opened) = &link_state {
                    match ipv6::receive(ipv6_state, opened, &device, buf, now).await {
                        Ok(new_state)
                        | Err(ProtocolError::InvalidAck(new_state))
                        | Err(ProtocolError::UnacceptableReq(new_state)) => {
                            ipv6_state = new_state;
                        }
                        error => {
                            panic!(
                                "Unexpected error when receiving IPv6 control packet {:?}",
                                error
                            );
                        }
                    }
                }
            }
            _ => {
                reject_identifier += 1;
                link::tx_protocol_rej(&device, buf, protocol, reject_identifier).await?
            }
        }
    }

    Ok(())
}

#[derive(Clone, Debug)]
struct TestDeviceInner {
    pub rx: Vec<Vec<u8>>,
    pub tx: Vec<Vec<u8>>,
    pub next_rx: usize,
    pub next_tx: usize,
}

impl Drop for TestDeviceInner {
    fn drop(&mut self) {
        assert_eq!(self.next_rx, self.rx.len());
        assert_eq!(self.next_tx, self.tx.len());
    }
}

#[derive(Clone, Debug)]
struct TestDevice(Arc<Mutex<TestDeviceInner>>);

impl TestDevice {
    fn new(rx: Vec<Vec<u8>>, tx: Vec<Vec<u8>>) -> Self {
        Self(Arc::new(Mutex::new(TestDeviceInner { rx, tx, next_rx: 0, next_tx: 0 })))
    }
}

impl FrameReceiver for TestDevice {
    fn rx_frame<'a>(&'a self) -> BoxFuture<'a, Result<Vec<u8>, FrameError>> {
        Box::pin(async move {
            let mut inner = self.0.lock().unwrap();
            assert_ne!(inner.next_rx, inner.rx.len());
            let res = Ok(inner.rx[inner.next_rx].clone());
            inner.next_rx += 1;
            res
        })
    }
}

impl FrameTransmitter for TestDevice {
    fn tx_frame<'a>(&'a self, frame: &'a [u8]) -> BoxFuture<'a, Result<(), FrameError>> {
        Box::pin(async move {
            let mut inner = self.0.lock().unwrap();
            assert_ne!(inner.next_tx, inner.tx.len());
            assert_eq!(inner.tx[inner.next_tx], frame);
            inner.next_tx += 1;
            Ok(())
        })
    }
}
