// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod authenticator;
mod supplicant;

use self::authenticator::Authenticator;
use self::supplicant::Supplicant;
use Error;
use eapol;
use failure;
use futures::future::Either;
use futures::{task, Async, Poll, Stream};
use key::exchange::Key;
use rsna::Role;
use rsne::Rsne;

enum RoleHandler {
    Authenticator(Authenticator),
    Supplicant(Supplicant),
}

#[derive(Debug)]
pub struct Config {
    role: Role,
    pmk: Vec<u8>,
    sta_addr: [u8; 6],
    sta_rsne: Rsne,
    peer_addr: [u8; 6],
    peer_rsne: Rsne,
}

impl Config {
    pub fn new(
        role: Role, pmk: Vec<u8>, sta_addr: [u8; 6], sta_rsne: Rsne, peer_addr: [u8; 6],
        peer_rsne: Rsne,
    ) -> Result<Config, failure::Error> {
        // TODO(hahnr): Validate configuration for:
        // (1) Correct RSNE subset
        // (2) Correct AKM and Cipher Suite configuration
        // (3) Valid PMK for negotiated AKM
        Ok(Config {
            role,
            pmk,
            sta_addr,
            sta_rsne,
            peer_addr,
            peer_rsne,
        })
    }
}

pub struct Fourway {
    cfg: Config,
    handler: RoleHandler,
    ptk: Vec<u8>,
    completed: bool,
}

impl Fourway {
    pub fn new(cfg: Config) -> Result<Fourway, failure::Error> {
        let handler = match &cfg.role {
            &Role::Supplicant => RoleHandler::Supplicant(Supplicant::new()?),
            &Role::Authenticator => RoleHandler::Authenticator(Authenticator::new()?),
        };
        Ok(Fourway {
            cfg,
            handler,
            ptk: vec![],
            completed: false,
        })
    }

    pub fn initiate(&self) -> Result<(), failure::Error> {
        match &self.handler {
            &RoleHandler::Authenticator(ref a) => a.initiate(),
            _ => Err(Error::UnexpectedInitiationRequest.into()),
        }
    }

    pub fn completed(&self) -> bool {
        self.completed
    }
}

impl eapol::KeyFrameReceiver for Fourway {
    fn on_eapol_key_frame(&self, _frame: &eapol::KeyFrame) -> Result<(), failure::Error> {
        // TODO(hahnr): Forward to active handler.
        Ok(())
    }
}

impl Stream for Fourway {
    type Item = Either<eapol::Frame, Key>;
    type Error = failure::Error;

    fn poll_next(&mut self, _cx: &mut task::Context) -> Poll<Option<Self::Item>, Self::Error> {
        // TODO(hahnr): Forward to active handler.
        Ok(Async::Pending)
    }
}
