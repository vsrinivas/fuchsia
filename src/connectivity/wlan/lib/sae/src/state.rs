// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        boringssl::{Bignum, BignumCtx},
        frame::{write_commit, write_confirm},
        internal::FiniteCyclicGroup,
        internal::SaeParameters,
        CommitMsg, ConfirmMsg, Key, RejectReason, SaeHandshake, SaeUpdate, SaeUpdateSink, Timeout,
    },
    crate::crypto_utils::kdf_sha256,
    anyhow::{bail, format_err, Error},
    log::{error, warn},
    wlan_statemachine::*,
};

// TODO(42140): Handle received timeouts.
// TODO(42141): Process/send anti-clogging tokens.
// TODO(42562): Handle BadGrp/DiffGrp.
// TODO(42563): Handle frame status.

/// We store an FcgConstructor rather than a FiniteCyclicGroup so that our handshake
/// can impl `Send`. FCGs are not generally `Send`, so we construct them on the fly.
type FcgConstructor<E> =
    Box<dyn Fn() -> Result<Box<dyn FiniteCyclicGroup<Element = E>>, Error> + Send + 'static>;

struct SaeConfiguration<E> {
    fcg: FcgConstructor<E>,
    params: SaeParameters,
    pwe: Vec<u8>,
}

struct Commit<E> {
    scalar: Bignum,
    element: E,
}

struct SerializedCommit {
    scalar: Vec<u8>,
    element: Vec<u8>,
}

#[derive(Debug, PartialEq)]
struct Kck(Vec<u8>);

impl<E> Commit<E> {
    /// IEEE 802.11-2016 12.4.7.4
    /// Returns the serialized scalar and element with appropriate padding as needed.
    fn serialize(&self, config: &SaeConfiguration<E>) -> Result<SerializedCommit, Error> {
        let fcg = (config.fcg)()?;
        let scalar_size = fcg.scalar_size()?;
        let scalar = self.scalar.to_left_padded_vec(scalar_size);
        let element = fcg.element_to_octets(&self.element)?;
        Ok(SerializedCommit { scalar, element })
    }
}

impl SerializedCommit {
    fn deserialize<E>(&self, config: &SaeConfiguration<E>) -> Result<Commit<E>, Error> {
        let fcg = (config.fcg)()?;
        let scalar = Bignum::new_from_slice(&self.scalar[..])?;
        let element = match fcg.element_from_octets(&self.element)? {
            Some(element) => element,
            None => bail!("Attempted to deserialize invalid FCG element"),
        };
        Ok(Commit { scalar, element })
    }
}

struct SaeNew<E> {
    config: SaeConfiguration<E>,
}

struct SaeCommitted<E> {
    config: SaeConfiguration<E>,
    rand: Vec<u8>,
    commit: SerializedCommit,
    sync: u16,
}

struct SaeConfirmed<E> {
    config: SaeConfiguration<E>,
    commit: SerializedCommit,
    peer_commit: SerializedCommit,
    kck: Kck,
    key: Key,
    sc: u16, // send confirm
    rc: u16, // receive confirm
    sync: u16,
}

// Everything is finished in this state. We keep around the old SaeConfirmed struct in case we need
// to replay our confirm frame.
struct SaeAccepted<E>(SaeConfirmed<E>);
struct SaeFailed;

statemachine!(
    enum SaeHandshakeState<E>,
    () => SaeNew<E>,
    SaeNew<E> => [SaeCommitted<E>, SaeConfirmed<E>, SaeFailed],
    // SaeCommitted does not self-loop because a retry does not update any state.
    SaeCommitted<E> => [SaeConfirmed<E>, SaeFailed],
    // SaeConfirmed can self-loop because retries increment send_confirm.
    SaeConfirmed<E> => [SaeAccepted<E>, SaeFailed, SaeConfirmed<E>],
    SaeAccepted<E> => SaeFailed,
);

/// This enum is used in any place where an operation can either succeed, or result in silently
/// dropping the received frame.
enum FrameResult<T> {
    /// The frame was processed and the given output produced as a result.
    Proceed(T),
    /// The frame was incorrect and should be dropped silently.
    Drop,
}

impl<T> std::fmt::Debug for FrameResult<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Proceed(_) => write!(f, "FrameResult::Proceed"),
            Self::Drop => write!(f, "FrameResult::Drop"),
        }
    }
}

/// IEEE 802.11-2016 12.4.5.4
/// Returns the calculated pairwise key and peer commit, or None if the given peer element is
/// invalid.
fn process_commit<E>(
    config: &SaeConfiguration<E>,
    rand: &Bignum,
    commit: &Commit<E>,
    peer_scalar: &[u8],
    peer_element: &[u8],
) -> Result<FrameResult<(Commit<E>, Kck, Key)>, RejectReason> {
    let fcg = (config.fcg)()?;
    // Parse the peer element.
    let peer_commit = match fcg.element_from_octets(peer_element)? {
        Some(element) => Commit { scalar: Bignum::new_from_slice(peer_scalar)?, element },
        None => return Ok(FrameResult::Drop),
    };

    let pwe = fcg.element_from_octets(&config.pwe)?.ok_or(format_err!("Could not unwrap PWE"))?;
    let element_k = fcg.scalar_op(
        rand,
        &fcg.elem_op(&fcg.scalar_op(&peer_commit.scalar, &pwe)?, &peer_commit.element)?,
    )?;
    let k = match fcg.map_to_secret_value(&element_k)? {
        Some(k) => k,
        None => return Ok(FrameResult::Drop), // This is an auth failure.
    };
    let ctx = BignumCtx::new()?;
    let keyseed = (config.params.h)(&[0u8; 32][..], &k.to_vec()[..]);
    let sha_ctx = peer_commit.scalar.mod_add(&commit.scalar, &fcg.order()?, &ctx)?.to_vec();
    let kck_and_pmk = kdf_sha256(&keyseed[..], "SAE KCK and PMK", &sha_ctx[..], 512);
    let kck = kck_and_pmk[0..32].to_vec();
    let pmk = kck_and_pmk[32..64].to_vec();
    let pmkid = sha_ctx[0..16].to_vec();
    Ok(FrameResult::Proceed((peer_commit, Kck(kck), Key { pmk, pmkid })))
}

/// IEEE 802.11-2016 12.4.5.{5,6}
/// Computes the confirm value for sending or validating a confirm message. This can only fail from
/// an internal error.
fn compute_confirm<E>(
    config: &SaeConfiguration<E>,
    kck: &Kck,
    send_confirm: u16,
    commit1: &SerializedCommit,
    commit2: &SerializedCommit,
) -> Result<Vec<u8>, RejectReason> {
    Ok((config.params.cn)(
        &kck.0[..],
        send_confirm,
        vec![&commit1.scalar[..], &commit1.element[..], &commit2.scalar[..], &commit2.element[..]],
    ))
}

/// Helper function to reject the authentication after too many retries.
fn check_sync(sync: &u16) -> Result<(), RejectReason> {
    // IEEE says we should only fail if sync exceeds our limit, but failing on equality as well gives
    // MAX_RETRIES_PER_EXCHANGE slightly more obvious behavior.
    if *sync >= super::MAX_RETRIES_PER_EXCHANGE {
        Err(RejectReason::TooManyRetries)
    } else {
        Ok(())
    }
}

/// IEEE 802.11-2016 12.4.5.3
impl<E> SaeNew<E> {
    fn commit(&self) -> Result<(Vec<u8>, SerializedCommit), Error> {
        let fcg = (self.config.fcg)()?;
        let order = fcg.order()?;
        let ctx = BignumCtx::new()?;
        let (rand, mask, scalar) = loop {
            // 2 < rand < order
            let rand = Bignum::rand(&order.sub(Bignum::new_from_u64(3)?)?)?
                .add(Bignum::new_from_u64(3)?)?;
            // 1 < mask < rand
            let mask = Bignum::rand(&rand.sub(Bignum::new_from_u64(2)?)?)?
                .add(Bignum::new_from_u64(2)?)?;
            let commit_scalar = rand.mod_add(&mask, &order, &ctx)?;
            if !commit_scalar.is_zero() && !commit_scalar.is_one() {
                break (rand, mask, commit_scalar);
            }
        };
        let pwe = fcg
            .element_from_octets(&self.config.pwe)?
            .ok_or(format_err!("Could not unwrap PWE"))?;
        let element = fcg.inverse_op(fcg.scalar_op(&mask, &pwe)?)?;
        Ok((rand.to_vec(), Commit { scalar, element }.serialize(&self.config)?))
    }

    fn send_first_commit(
        &self,
        sink: &mut SaeUpdateSink,
    ) -> Result<(Vec<u8>, SerializedCommit), RejectReason> {
        let (rand, commit) = self.commit()?;
        let group_id = (self.config.fcg)()?.group_id();
        sink.push(SaeUpdate::SendFrame(write_commit(
            group_id,
            &commit.scalar[..],
            &commit.element[..],
            &[],
        )));
        sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
        Ok((rand, commit))
    }

    fn handle_commit(
        &self,
        sink: &mut SaeUpdateSink,
        commit_msg: &CommitMsg,
    ) -> Result<(Vec<u8>, SerializedCommit, SerializedCommit, Kck, Key), RejectReason> {
        let (serialized_rand, serialized_commit) = self.commit()?;
        let commit = serialized_commit.deserialize(&self.config)?;
        let rand = Bignum::new_from_slice(&serialized_rand[..])?;
        let (peer_commit, kck, key) = match process_commit(
            &self.config,
            &rand,
            &commit,
            &commit_msg.scalar[..],
            &commit_msg.element[..],
        )? {
            FrameResult::Proceed(res) => res,
            // If we drop the first frame, reject the authentication immediately.
            FrameResult::Drop => return Err(RejectReason::AuthFailed),
        };
        let peer_commit = peer_commit.serialize(&self.config)?;
        let confirm = compute_confirm(&self.config, &kck, 1, &serialized_commit, &peer_commit)?;
        // We do not send our own commit message unless we process the peer's successfully.
        let group_id = (self.config.fcg)()?.group_id();
        sink.push(SaeUpdate::SendFrame(write_commit(
            group_id,
            &serialized_commit.scalar[..],
            &serialized_commit.element[..],
            &[],
        )));
        sink.push(SaeUpdate::SendFrame(write_confirm(1, &confirm[..])));
        sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
        Ok((serialized_rand, serialized_commit, peer_commit, kck, key))
    }
}

/// IEEE 802.11-2016 12.4.8.6.4
impl<E> SaeCommitted<E> {
    fn handle_commit(
        &self,
        sink: &mut SaeUpdateSink,
        commit_msg: &CommitMsg,
    ) -> Result<FrameResult<(SerializedCommit, Kck, Key)>, RejectReason> {
        if &commit_msg.scalar[..] == &self.commit.scalar[..]
            && &commit_msg.element[..] == &self.commit.element[..]
        {
            // This is a reflection attack.
            sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
            return Ok(FrameResult::Drop);
        }
        let (peer_commit, kck, key) = match process_commit(
            &self.config,
            &Bignum::new_from_slice(&self.rand[..])?,
            &self.commit.deserialize(&self.config)?,
            &commit_msg.scalar[..],
            &commit_msg.element[..],
        )? {
            FrameResult::Proceed(res) => res,
            // IEEE doesn't specify that we do anything in this case. It might make sense to reset
            // the retransmission timer, but we stick with the spec.
            FrameResult::Drop => return Ok(FrameResult::Drop),
        };
        let peer_commit = peer_commit.serialize(&self.config)?;
        let confirm = compute_confirm(&self.config, &kck, 1, &self.commit, &peer_commit)?;
        sink.push(SaeUpdate::SendFrame(write_confirm(1, &confirm[..])));
        sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
        Ok(FrameResult::Proceed((peer_commit, kck, key)))
    }

    fn resend_last_frame(&mut self, sink: &mut SaeUpdateSink) -> Result<(), RejectReason> {
        check_sync(&self.sync)?;
        self.sync += 1;
        // We resend our last commit.
        let group_id = (self.config.fcg)()?.group_id();
        sink.push(SaeUpdate::SendFrame(write_commit(
            group_id,
            &self.commit.scalar[..],
            &self.commit.element[..],
            &[],
        )));
        sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
        Ok(())
    }

    fn handle_confirm(
        &mut self,
        sink: &mut SaeUpdateSink,
        _confirm_msg: &ConfirmMsg,
    ) -> Result<(), RejectReason> {
        self.resend_last_frame(sink)
    }

    fn handle_timeout(
        &mut self,
        sink: &mut SaeUpdateSink,
        timeout: Timeout,
    ) -> Result<(), RejectReason> {
        match timeout {
            Timeout::Retransmission => self.resend_last_frame(sink),
            Timeout::KeyExpiration => {
                Err(format_err!("Unexpected key expiration timout before PMKSA established.")
                    .into())
            }
        }
    }
}

/// IEEE 802.11-2016 12.4.8.6.5
impl<E> SaeConfirmed<E> {
    fn handle_commit(
        &mut self,
        sink: &mut SaeUpdateSink,
        _commit_msg: &CommitMsg,
    ) -> Result<(), RejectReason> {
        // The peer did not receive our previous commit or confirm.
        check_sync(&self.sync)?;
        // IEEE Std 802.11 does *not* specify that we verify the peer sent the same commit. We just
        // assume this to be the case.
        self.sync += 1;
        self.sc += 1;
        let confirm =
            compute_confirm(&self.config, &self.kck, self.sc, &self.commit, &self.peer_commit)?;
        let group_id = (self.config.fcg)()?.group_id();
        sink.push(SaeUpdate::SendFrame(write_commit(
            group_id,
            &self.commit.scalar[..],
            &self.commit.element[..],
            &[],
        )));
        sink.push(SaeUpdate::SendFrame(write_confirm(self.sc, &confirm[..])));
        sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
        Ok(())
    }

    fn handle_confirm(
        &mut self,
        sink: &mut SaeUpdateSink,
        confirm_msg: &ConfirmMsg,
    ) -> Result<FrameResult<()>, RejectReason> {
        let verifier = compute_confirm(
            &self.config,
            &self.kck,
            confirm_msg.send_confirm,
            &self.peer_commit,
            &self.commit,
        )?;
        if confirm_msg.confirm == &verifier[..] {
            sink.push(SaeUpdate::CancelTimeout(Timeout::Retransmission));
            sink.push(SaeUpdate::ResetTimeout(Timeout::KeyExpiration));
            self.rc = confirm_msg.send_confirm;
            // We use u16::max_value() where IEEE specifies 2^16 - 1.
            self.sc = u16::max_value();
            sink.push(SaeUpdate::Success(self.key.clone()));
            Ok(FrameResult::Proceed(()))
        } else {
            Ok(FrameResult::Drop)
        }
    }

    fn handle_timeout(
        &mut self,
        sink: &mut SaeUpdateSink,
        timeout: Timeout,
    ) -> Result<(), RejectReason> {
        match timeout {
            Timeout::Retransmission => {
                // Resend our confirm message.
                check_sync(&self.sync)?;
                self.sync += 1;
                self.sc += 1;
                let confirm = compute_confirm(
                    &self.config,
                    &self.kck,
                    self.sc,
                    &self.commit,
                    &self.peer_commit,
                )?;
                sink.push(SaeUpdate::SendFrame(write_confirm(self.sc, &confirm[..])));
                sink.push(SaeUpdate::ResetTimeout(Timeout::Retransmission));
                Ok(())
            }
            Timeout::KeyExpiration => {
                Err(format_err!("Unexpected key expiration timout before PMKSA established.")
                    .into())
            }
        }
    }
}

/// IEEE 802.11-2016 12.4.8.6.6
impl<E> SaeAccepted<E> {
    // This function does not return a FrameResult because there is no state transition in the
    // successful case.
    fn handle_confirm(
        &mut self,
        sink: &mut SaeUpdateSink,
        confirm_msg: &ConfirmMsg,
    ) -> Result<(), RejectReason> {
        check_sync(&mut self.0.sync)?;
        // We use u16::max_value() where IEEE specifies 2^16 - 1.
        if confirm_msg.send_confirm <= self.0.rc || confirm_msg.send_confirm == u16::max_value() {
            return Ok(());
        }
        // If we fail to verify, the message is dropped silently.
        if let Ok(verifier) = compute_confirm(
            &self.0.config,
            &self.0.kck,
            confirm_msg.send_confirm,
            &self.0.peer_commit,
            &self.0.commit,
        ) {
            if verifier == &confirm_msg.confirm[..] {
                self.0.rc = confirm_msg.send_confirm;
                self.0.sync += 1;
                let confirm = compute_confirm(
                    &self.0.config,
                    &self.0.kck,
                    self.0.sc,
                    &self.0.commit,
                    &self.0.peer_commit,
                )?;
                sink.push(SaeUpdate::SendFrame(write_confirm(self.0.sc, &confirm[..])));
            }
        }
        Ok(())
    }

    fn handle_timeout(
        &mut self,
        sink: &mut SaeUpdateSink,
        timeout: Timeout,
    ) -> Result<(), RejectReason> {
        match timeout {
            Timeout::Retransmission => {
                // This is weird, but probably shouldn't kill our PMKSA.
                error!("Unexpected retransmission timeout after completed SAE handshake.");
                Ok(())
            }
            Timeout::KeyExpiration => Err(RejectReason::KeyExpiration),
        }
    }
}

impl<E> SaeHandshakeState<E> {
    fn initiate_sae(self, sink: &mut SaeUpdateSink) -> Self {
        match self {
            SaeHandshakeState::SaeNew(state) => match state.send_first_commit(sink) {
                Ok((rand, commit)) => {
                    let (transition, state) = state.release_data();
                    transition
                        .to(SaeCommitted { config: state.config, rand, commit, sync: 0 })
                        .into()
                }
                Err(reject) => {
                    sink.push(SaeUpdate::Reject(reject));
                    state.transition_to(SaeFailed).into()
                }
            },
            _ => {
                error!("Unexpected call to initiate_sae");
                self
            }
        }
    }

    fn handle_commit(self, sink: &mut SaeUpdateSink, commit_msg: &CommitMsg) -> Self {
        match self {
            SaeHandshakeState::SaeNew(state) => {
                match state.handle_commit(sink, commit_msg) {
                    Ok((rand, commit, peer_commit, kck, key)) => {
                        let (transition, state) = state.release_data();
                        transition
                            .to(SaeConfirmed {
                                config: state.config,
                                commit,
                                peer_commit,
                                kck,
                                key,
                                sc: 1,
                                rc: 0,
                                sync: 0,
                            })
                            .into()
                    }
                    // We always reject the authentication if the first commit is invalid.
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            SaeHandshakeState::SaeCommitted(state) => match state.handle_commit(sink, commit_msg) {
                Ok(FrameResult::Proceed((peer_commit, kck, key))) => {
                    let (transition, committed) = state.release_data();
                    let confirmed = SaeConfirmed {
                        config: committed.config,
                        commit: committed.commit,
                        peer_commit,
                        kck,
                        key,
                        sc: 1,
                        rc: 0,
                        sync: committed.sync,
                    };
                    transition.to(confirmed).into()
                }
                Ok(FrameResult::Drop) => state.into(),
                Err(reject) => {
                    sink.push(SaeUpdate::Reject(reject));
                    state.transition_to(SaeFailed).into()
                }
            },
            SaeHandshakeState::SaeConfirmed(mut state) => {
                match state.handle_commit(sink, commit_msg) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            _ => {
                warn!("Unexpected SAE commit received");
                self
            }
        }
    }

    fn handle_confirm(self, sink: &mut SaeUpdateSink, confirm_msg: &ConfirmMsg) -> Self {
        match self {
            SaeHandshakeState::SaeCommitted(mut state) => {
                match state.handle_confirm(sink, confirm_msg) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            SaeHandshakeState::SaeConfirmed(mut state) => {
                match state.handle_confirm(sink, confirm_msg) {
                    Ok(FrameResult::Proceed(())) => {
                        let (transition, mut state) = state.release_data();
                        transition.to(SaeAccepted(state)).into()
                    }
                    Ok(FrameResult::Drop) => state.into(),
                    Err(e) => {
                        sink.push(SaeUpdate::Reject(e.into()));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            SaeHandshakeState::SaeAccepted(mut state) => {
                match state.handle_confirm(sink, confirm_msg) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            _ => {
                warn!("Unexpected SAE confirm received");
                self
            }
        }
    }

    fn handle_timeout(self, sink: &mut SaeUpdateSink, timeout: Timeout) -> Self {
        match self {
            SaeHandshakeState::SaeCommitted(mut state) => {
                match state.handle_timeout(sink, timeout) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            SaeHandshakeState::SaeConfirmed(mut state) => {
                match state.handle_timeout(sink, timeout) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            SaeHandshakeState::SaeAccepted(mut state) => {
                match state.handle_timeout(sink, timeout) {
                    Ok(()) => state.into(),
                    Err(reject) => {
                        sink.push(SaeUpdate::Reject(reject));
                        state.transition_to(SaeFailed).into()
                    }
                }
            }
            _ => {
                error!("Unexpected SAE timeout triggered");
                self
            }
        }
    }
}

pub struct SaeHandshakeImpl<E>(StateMachine<SaeHandshakeState<E>>);

impl<E> SaeHandshakeImpl<E> {
    pub fn new(fcg_constructor: FcgConstructor<E>, params: SaeParameters) -> Result<Self, Error> {
        let fcg = fcg_constructor()?;
        let pwe = fcg.element_to_octets(&fcg.generate_pwe(&params)?)?;
        Ok(Self(StateMachine::new(SaeHandshakeState::from(State::new(SaeNew {
            config: SaeConfiguration { fcg: fcg_constructor, params, pwe },
        })))))
    }
}

impl<E> SaeHandshake for SaeHandshakeImpl<E> {
    fn initiate_sae(&mut self, sink: &mut SaeUpdateSink) {
        self.0.replace_state(|state| state.initiate_sae(sink));
    }

    fn handle_commit(&mut self, sink: &mut SaeUpdateSink, commit_msg: &CommitMsg) {
        self.0.replace_state(|state| state.handle_commit(sink, commit_msg));
    }

    fn handle_confirm(&mut self, sink: &mut SaeUpdateSink, confirm_msg: &ConfirmMsg) {
        self.0.replace_state(|state| state.handle_confirm(sink, confirm_msg));
    }

    fn handle_timeout(&mut self, sink: &mut SaeUpdateSink, timeout: Timeout) {
        self.0.replace_state(|state| state.handle_timeout(sink, timeout));
    }
}

// Most testing is done in sae/mod.rs, so we only test internal functions here.
#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            self as sae,
            boringssl::{Bignum, EcGroupId},
            ecc,
        },
        hex::FromHex,
        wlan_common::{assert_variant, mac::MacAddr},
    };

    // IEEE 802.11-18/1104r0: "New Test Vectors for SAE" provides all of these values.
    const TEST_GROUP: EcGroupId = EcGroupId::P256;
    const TEST_PWD: &'static str = "mekmitasdigoatpsk4internet";
    const TEST_STA_A: MacAddr = [0x82, 0x7b, 0x91, 0x9d, 0xd4, 0xb9];
    const TEST_RAND_A: &'static str =
        "a906f61e4d3a5d4eb2965ff34cf917dd044445c878c17ca5d5b93786da9f83cf";
    const TEST_SCALAR_A: &'static str =
        "eb3bab1964e4a0ab05925ddf3339519138bc65d6cdc0f813dd6fd4344eb4bfe4";
    const TEST_ELEMENT_A: &'static str = "4b5c21597658f4e3eddfb4b99f25b4d6540f32ff1fd5c530c60a794448610bc6de3d92bdbbd47d935980ca6cf8988ab6630be6764c885ceb9793970f695217ee";
    const TEST_CONFIRM_A: &'static str =
        "12d9d5c78c500526d36c41dbc56aedf2914cedddd7cad4a58c48f83dbde9fc77";
    const TEST_STA_B: MacAddr = [0x1e, 0xec, 0x49, 0xea, 0x64, 0x88];
    const TEST_RAND_B: &'static str =
        "a47d07bbd3d1b618b325dfde02413a450a90fd1ee1ac35f4d3856cc9cb77128c";
    const TEST_SCALAR_B: &'static str =
        "5564f045b2ea1e566cf1dd741f70d9be35d2df5b9a5502946ee03cf8dae27e1e";
    const TEST_ELEMENT_B: &'static str = "05b8430eb7a99e24877ce69baf3dc580e309633d6b385f83ee1c3ec3591f1a5393c06e805ddceb2fde50930dd7cfebb987c6ff9666af164eb5184d8e6662ed6a";
    const TEST_CONFIRM_B: &'static str =
        "02871cf906898b8060ec184143be77b8c08a8019b13eb6d0aef0d8383dfac2fd";
    const KEY_PMK: &'static str =
        "7aead86fba4c3221fc437f5f14d70d854ea5d5aac1690116793081eda4d557c5";
    const KEY_KCK: &'static str =
        "599d6f1e27548be8499dceed2feccf94818ce1c79f1b4eb3d6a53228a09bf3ed";
    const KEY_PMKID: &'static str = "40a09b6017cebf0072843b5352aa2b4f";

    fn make_ecc_config() -> SaeConfiguration<<ecc::Group as FiniteCyclicGroup>::Element> {
        let params = SaeParameters {
            h: sae::h,
            cn: sae::cn,
            password: Vec::from(TEST_PWD),
            sta_a_mac: TEST_STA_A,
            sta_b_mac: TEST_STA_B,
        };
        let fcg_constructor = Box::new(|| {
            ecc::Group::new(TEST_GROUP).map(|group| {
                Box::new(group)
                    as Box<
                        dyn FiniteCyclicGroup<Element = <ecc::Group as FiniteCyclicGroup>::Element>,
                    >
            })
        });
        let fcg = (fcg_constructor)().unwrap();
        let pwe = fcg.element_to_octets(&fcg.generate_pwe(&params).unwrap()).unwrap();
        SaeConfiguration { fcg: fcg_constructor, params, pwe }
    }

    fn make_commit<E>(config: &SaeConfiguration<E>, scalar: &str, element: &str) -> Commit<E> {
        let scalar = Bignum::new_from_slice(&Vec::from_hex(scalar).unwrap()[..]).unwrap();
        let element = (config.fcg)()
            .unwrap()
            .element_from_octets(&Vec::from_hex(element).unwrap()[..])
            .unwrap()
            .unwrap();
        Commit { scalar, element }
    }

    fn expected_kck() -> Kck {
        Kck(Vec::from_hex(KEY_KCK).unwrap())
    }

    fn expected_key() -> Key {
        Key { pmk: Vec::from_hex(KEY_PMK).unwrap(), pmkid: Vec::from_hex(KEY_PMKID).unwrap() }
    }

    #[test]
    fn process_commit_success_sta_a() {
        let config = make_ecc_config();
        let commit_a = make_commit(&config, TEST_SCALAR_A, TEST_ELEMENT_A);

        let rand_a = Bignum::new_from_slice(&Vec::from_hex(TEST_RAND_A).unwrap()[..]).unwrap();
        let scalar_b = Vec::from_hex(TEST_SCALAR_B).unwrap();
        let element_b = Vec::from_hex(TEST_ELEMENT_B).unwrap();

        let result =
            process_commit(&config, &rand_a, &commit_a, &scalar_b[..], &element_b[..]).unwrap();
        let (_peer_commit_a, kck, key) = assert_variant!(result, FrameResult::Proceed(res) => res);

        assert_eq!(kck, expected_kck());
        assert_eq!(key, expected_key());
    }

    #[test]
    fn process_commit_success_sta_b() {
        let config = make_ecc_config();
        let commit_b = make_commit(&config, TEST_SCALAR_B, TEST_ELEMENT_B);

        let rand_b = Bignum::new_from_slice(&Vec::from_hex(TEST_RAND_B).unwrap()[..]).unwrap();
        let scalar_a = Vec::from_hex(TEST_SCALAR_A).unwrap();
        let element_a = Vec::from_hex(TEST_ELEMENT_A).unwrap();

        let result =
            process_commit(&config, &rand_b, &commit_b, &scalar_a[..], &element_a[..]).unwrap();
        let (_peer_commit_b, kck, key) = assert_variant!(result, FrameResult::Proceed(res) => res);

        assert_eq!(kck, expected_kck());
        assert_eq!(key, expected_key());
    }

    #[test]
    fn process_commit_fails_bad_peer_element() {
        let config = make_ecc_config();
        let commit_a = make_commit(&config, TEST_SCALAR_A, TEST_ELEMENT_A);

        let rand_a = Bignum::new_from_slice(&Vec::from_hex(TEST_RAND_A).unwrap()[..]).unwrap();
        let scalar_b = Vec::from_hex(TEST_SCALAR_B).unwrap();
        let mut element_b = Vec::from_hex(TEST_ELEMENT_B).unwrap();
        element_b[0] += 1;

        let result =
            process_commit(&config, &rand_a, &commit_a, &scalar_b[..], &element_b[..]).unwrap();
        assert_variant!(result, FrameResult::Drop);
    }

    #[test]
    fn test_compute_confirm() {
        let config = make_ecc_config();
        let commit_a =
            make_commit(&config, TEST_SCALAR_A, TEST_ELEMENT_A).serialize(&config).unwrap();
        let commit_b =
            make_commit(&config, TEST_SCALAR_B, TEST_ELEMENT_B).serialize(&config).unwrap();
        let kck = expected_kck();

        let confirm_a = compute_confirm(&config, &kck, 1, &commit_a, &commit_b).unwrap();
        let expected_confirm_a = Vec::from_hex(TEST_CONFIRM_A).unwrap();
        assert_eq!(confirm_a, expected_confirm_a);

        let confirm_b = compute_confirm(&config, &kck, 1, &commit_b, &commit_a).unwrap();
        let expected_confirm_b = Vec::from_hex(TEST_CONFIRM_B).unwrap();
        assert_eq!(confirm_b, expected_confirm_b);
    }
}
