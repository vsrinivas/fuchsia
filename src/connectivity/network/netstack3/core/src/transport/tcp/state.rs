// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TCP state machine per [RFC 793](https://tools.ietf.org/html/rfc793).
// Note: All RFC quotes (with two extra spaces at the beginning of each line) in
// this file are from https://tools.ietf.org/html/rfc793#section-3.9 if not
// specified otherwise.

use core::{convert::TryFrom as _, num::TryFromIntError};

use explicit::ResultExt as _;

use crate::transport::tcp::{
    buffer::{Assembler, ReceiveBuffer},
    segment::{Payload, Segment},
    seqnum::{SeqNum, WindowSize},
    Control, UserError,
};

/// Per RFC 793: https://tools.ietf.org/html/rfc793#page-22:
///
///   CLOSED - represents no connection state at all.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Closed<Error> {
    /// Describes a reason why the connection was closed.
    reason: Error,
}

impl Closed<()> {
    /// Corresponds to the [OPEN](https://tools.ietf.org/html/rfc793#page-54)
    /// user call.
    ///
    /// `iss`is The initial send sequence number. Which is effectively the
    /// sequence number of SYN.
    fn connect(iss: SeqNum) -> (SynSent, Segment<()>) {
        (SynSent { iss }, Segment::syn(iss, WindowSize::DEFAULT))
    }

    fn listen(iss: SeqNum) -> Listen {
        Listen { iss }
    }
}

impl<Error> Closed<Error> {
    /// Processes an incoming segment in the CLOSED state.
    ///
    /// TCP will either drop the incoming segment or generate a RST.
    fn on_segment(
        &self,
        Segment { seq: seg_seq, ack: seg_ack, wnd: _, contents }: Segment<impl Payload>,
    ) -> Option<Segment<()>> {
        // Per RFC 793 (https://tools.ietf.org/html/rfc793#page-65):
        //   If the state is CLOSED (i.e., TCB does not exist) then
        //   all data in the incoming segment is discarded.  An incoming
        //   segment containing a RST is discarded.  An incoming segment
        //   not containing a RST causes a RST to be sent in response.
        //   The acknowledgment and sequence field values are selected to
        //   make the reset sequence acceptable to the TCP that sent the
        //   offending segment.
        //   If the ACK bit is off, sequence number zero is used,
        //    <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>
        //   If the ACK bit is on,
        //    <SEQ=SEG.ACK><CTL=RST>
        //   Return.
        if contents.control() == Some(Control::RST) {
            return None;
        }
        Some(match seg_ack {
            Some(seg_ack) => Segment::rst(seg_ack),
            None => Segment::rst_ack(SeqNum::from(0), seg_seq + contents.len()),
        })
    }
}

/// Per RFC 793: https://tools.ietf.org/html/rfc793#page-21:
///
///   SYN-SENT - represents waiting for a matching connection request
///   after having sent a connection request.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Listen {
    iss: SeqNum,
}

impl Listen {
    fn on_segment(
        &self,
        Segment { seq, ack, wnd: _, contents }: Segment<impl Payload>,
    ) -> (Option<SynRcvd>, Option<Segment<()>>) {
        let Listen { iss } = *self;
        //   first check for an RST
        //   An incoming RST should be ignored.  Return.
        if contents.control() == Some(Control::RST) {
            return (None, None);
        }
        if let Some(ack) = ack {
            //   second check for an ACK
            //   Any acknowledgment is bad if it arrives on a connection still in
            //   the LISTEN state.  An acceptable reset segment should be formed
            //   for any arriving ACK-bearing segment.  The RST should be
            //   formatted as follows:
            //     <SEQ=SEG.ACK><CTL=RST>
            //   Return.
            return (None, Some(Segment::rst(ack)));
        }
        if contents.control() == Some(Control::SYN) {
            //   third check for a SYN
            //   Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
            //   control or text should be queued for processing later.  ISS
            //   should be selected and a SYN segment sent of the form:
            //     <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
            //   SND.NXT is set to ISS+1 and SND.UNA to ISS.  The connection
            //   state should be changed to SYN-RECEIVED.  Note that any other
            //   incoming control or data (combined with SYN) will be processed
            //   in the SYN-RECEIVED state, but processing of SYN and ACK should
            //   not be repeated.
            // Note: We don't support data being tranmistted in this state, so
            // there is no need to store these the RCV and SND variables.
            return (
                Some(SynRcvd { iss, irs: seq }),
                Some(Segment::syn_ack(iss, seq + 1, WindowSize::DEFAULT)),
            );
        }
        (None, None)
    }
}

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct SynSent {
    iss: SeqNum,
}

impl SynSent {
    /// Processes an incoming segment in the SYN-SENT state.
    ///
    /// Transitions to ESTABLSHED if the incoming segment is a proper SYN-ACK.
    /// Transitions to SYN-RCVD if the incoming segment is a SYN. Otherwise,
    /// the segment is dropped or an RST is generated.
    fn on_segment<R: ReceiveBuffer>(
        &self,
        Segment { seq: seg_seq, ack: seg_ack, wnd: seg_wnd, contents }: Segment<impl Payload>,
    ) -> (Option<Either<Either<Established<R>, SynRcvd>, Closed<UserError>>>, Option<Segment<()>>)
    {
        let Self { iss } = *self;
        // Per RFC 793 (https://tools.ietf.org/html/rfc793#page-65):
        //   first check the ACK bit
        //   If the ACK bit is set
        //     If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset (unless
        //     the RST bit is set, if so drop the segment and return)
        //       <SEQ=SEG.ACK><CTL=RST>
        //     and discard the segment.  Return.
        //     If SND.UNA =< SEG.ACK =< SND.NXT then the ACK is acceptable.
        let has_ack = match seg_ack {
            Some(ack) => {
                // In our implementation, because we don't carry data in our
                // initial SYN segment, SND.UNA == ISS, SND.NXT == ISS+1.
                if ack.before(iss) || ack.after(iss + 1) {
                    return if contents.control() == Some(Control::RST) {
                        (None, None)
                    } else {
                        (
                            Some(Either::B(Closed { reason: UserError::ConnectionReset })),
                            Some(Segment::rst(ack)),
                        )
                    };
                }
                true
            }
            None => false,
        };

        match contents.control() {
            Some(Control::RST) => {
                //   second check the RST bit
                //   If the RST bit is set
                //     If the ACK was acceptable then signal the user "error:
                //     connection reset", drop the segment, enter CLOSED state,
                //     delete TCB, and return.  Otherwise (no ACK) drop the
                //     segment and return.
                if has_ack {
                    (Some(Either::B(Closed { reason: UserError::ConnectionReset })), None)
                } else {
                    (None, None)
                }
            }
            Some(Control::SYN) => {
                //   fourth check the SYN bit
                //   This step should be reached only if the ACK is ok, or there
                //   is no ACK, and it [sic] the segment did not contain a RST.
                match seg_ack {
                    Some(ack) => {
                        //   If the SYN bit is on and the security/compartment
                        //   and precedence are acceptable then, RCV.NXT is set
                        //   to SEG.SEQ+1, IRS is set to SEG.SEQ.  SND.UNA
                        //   should be advanced to equal SEG.ACK (if there is an
                        //   ACK), and any segments on the retransmission queue
                        //   which are thereby acknowledged should be removed.

                        //   If SND.UNA > ISS (our SYN has been ACKed), change
                        //   the connection state to ESTABLISHED, form an ACK
                        //   segment
                        //     <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
                        //   and send it.  Data or controls which were queued
                        //   for transmission may be included.  If there are
                        //   other controls or text in the segment then
                        //   continue processing at the sixth step below where
                        //   the URG bit is checked, otherwise return.
                        if ack.after(iss) {
                            let irs = seg_seq;
                            let established = Established {
                                snd: Send { nxt: iss + 1, una: ack, wnd: seg_wnd },
                                rcv: Recv {
                                    buffer: R::default(),
                                    assembler: Assembler::new(irs + 1),
                                },
                            };
                            let ack_seg = Segment::ack(
                                established.snd.nxt,
                                established.rcv.nxt(),
                                established.rcv.wnd(),
                            );
                            (Some(Either::A(Either::A(established))), Some(ack_seg))
                        } else {
                            (None, None)
                        }
                    }
                    None => {
                        //   Otherwise enter SYN-RECEIVED, form a SYN,ACK
                        //   segment
                        //     <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
                        //   and send it.  If there are other controls or text
                        //   in the segment, queue them for processing after the
                        //   ESTABLISHED state has been reached, return.
                        (
                            Some(Either::A(Either::B(SynRcvd { iss, irs: seg_seq }))),
                            Some(Segment::syn_ack(iss, seg_seq + 1, WindowSize::DEFAULT)),
                        )
                    }
                }
            }
            //   fifth, if neither of the SYN or RST bits is set then drop the
            //   segment and return.
            Some(Control::FIN) | None => (None, None),
        }
    }
}

/// Per RFC 793: https://tools.ietf.org/html/rfc793#page-21:
///
///   SYN-RECEIVED - represents waiting for a confirming connection
///   request acknowledgment after having both received and sent a
///   connection request.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct SynRcvd {
    iss: SeqNum,
    irs: SeqNum,
}

impl SynRcvd {
    fn on_segment<R: ReceiveBuffer>(
        &self,
        incoming: Segment<impl Payload>,
    ) -> (Option<Either<Either<Established<R>, Listen>, Closed<UserError>>>, Option<Segment<()>>)
    {
        let SynRcvd { iss, irs } = *self;
        let is_rst = incoming.contents.control() == Some(Control::RST);
        let incoming = match incoming.overlap(irs + 1, WindowSize::DEFAULT) {
            Some(incoming) => incoming,
            None => {
                //   If an incoming segment is not acceptable, an acknowledgment
                //   should be sent in reply (unless the RST bit is set, if so drop
                //   the segment and return):
                //     <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
                //   After sending the acknowledgment, drop the unacceptable segment
                //   and return.
                return if is_rst {
                    (None, None)
                } else {
                    (None, Some(Segment::ack(iss + 1, irs + 1, WindowSize::DEFAULT)))
                };
            }
        };
        match incoming.contents.control() {
            Some(Control::RST) => {
                //  second check the RST bit,
                //  SYN-RECEIVED STATE
                //  If the RST bit is set
                //  If this connection was initiated with a passive OPEN (i.e.,
                //  came from the LISTEN state), then return this connection to
                //  LISTEN state and return.  The user need not be informed.  If
                //  this connection was initiated with an active OPEN (i.e., came
                //  from SYN-SENT state) then the connection was refused, signal
                //  the user "connection refused".  In either case, all segments
                //  on the retransmission queue should be removed.  And in the
                //  active OPEN case, enter the CLOSED state and delete the TCB,
                //  and return.
                // Note: When advancing LISTEN to SYN-RCVD, we will create a new
                // socket, so closing the new socket would be effectively going
                // back to LISTEN state since the socket with LISTEN state is
                // still alive. Also because the connection is never established,
                // the socket cannot be in the accept queue and the users won't
                // be informed. Given the reasons above, we don't explicitly
                // track if SYN-RCVD was from a passive OPEN or not.
                (Some(Either::B(Closed { reason: UserError::ConnectionReset })), None)
            }
            Some(Control::SYN) => {
                //   If the SYN is in the window it is an error, send a reset, any
                //   outstanding RECEIVEs and SEND should receive "reset" responses,
                //   all segment queues should be flushed, the user should also
                //   receive an unsolicited general "connection reset" signal, enter
                //   the CLOSED state, delete the TCB, and return.
                (
                    Some(Either::B(Closed { reason: UserError::ConnectionReset })),
                    Some(Segment::rst_ack(iss, irs)),
                )
            }
            None => {
                //  if the ACK bit is on
                //  SYN-RECEIVED STATE
                //  If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state
                //  and continue processing.
                //    If the segment acknowledgment is not acceptable, form a
                //    reset segment,
                //      <SEQ=SEG.ACK><CTL=RST>
                //    and send it.
                // Note: We don't support sending data with SYN, so we don't
                // store the `SND` variables because they can be easily derived
                // from ISS: SND.UNA=ISS and SND.NXT=ISS+1.
                match incoming.ack {
                    Some(ack) => {
                        // NOTE: The RFC technically allows the state machine
                        // to transition to established if our SYN is not acked
                        // (SEG.ACK = SND.UNA). Linux doesn't follow the RFC
                        // and has a more intuitive behavior: the only ACK #
                        // that is accepatable is ISS+1 (SND.NXT).
                        if ack != iss + 1 {
                            (None, Some(Segment::rst(ack)))
                        } else {
                            (
                                Some(Either::A(Either::A(Established {
                                    snd: Send { nxt: iss + 1, una: ack, wnd: WindowSize::DEFAULT },
                                    rcv: Recv {
                                        buffer: R::default(),
                                        assembler: Assembler::new(irs + 1),
                                    },
                                }))),
                                None,
                            )
                        }
                    }
                    //   if the ACK bit is off drop the segment and return
                    None => (None, None),
                }
            }
            Some(Control::FIN) => {
                todo!("https://fxbug.dev/93522: should transition into CLOSE_WAIT")
            }
        }
    }
}

/// TCP control block variables that are responsible for sending.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Send {
    nxt: SeqNum,
    una: SeqNum,
    wnd: WindowSize,
}

/// TCP control block variables that are responsible for receiving.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Recv<R: ReceiveBuffer> {
    buffer: R,
    assembler: Assembler,
}

impl<R: ReceiveBuffer> Recv<R> {
    fn wnd(&self) -> WindowSize {
        let wnd = self.buffer.cap() - self.buffer.len();
        u32::try_from(wnd)
            .ok_checked::<TryFromIntError>()
            .and_then(WindowSize::new)
            .unwrap_or(WindowSize::MAX)
    }

    fn nxt(&self) -> SeqNum {
        self.assembler.nxt()
    }
}

/// Per RFC 793: https://tools.ietf.org/html/rfc793#page-22:
///
///   ESTABLISHED - represents an open connection, data received can be
///   delivered to the user.  The normal state for the data transfer phase
///   of the connection.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Established<R: ReceiveBuffer> {
    snd: Send,
    rcv: Recv<R>,
}

impl<R: ReceiveBuffer> Established<R> {
    fn on_segment(
        &mut self,
        incoming: Segment<impl Payload>,
    ) -> (Option<Closed<UserError>>, Option<Segment<()>>) {
        let Self { snd, rcv } = self;
        let Segment { seq, ack, wnd: _, contents } = match incoming.overlap(rcv.nxt(), rcv.wnd()) {
            None => return (None, Some(Segment::ack(snd.nxt, rcv.nxt(), rcv.wnd()))),
            Some(seg) => seg,
        };
        //   If the RST bit is set then, any outstanding RECEIVEs and SEND
        //   should receive "reset" responses.  All segment queues should be
        //   flushed.  Users should also receive an unsolicited general
        //   "connection reset" signal.  Enter the CLOSED state, delete the
        //   TCB, and return.
        if contents.control() == Some(Control::RST) {
            return (Some(Closed { reason: UserError::ConnectionReset }), None);
        }
        //   If the SYN is in the window it is an error, send a reset, any
        //   outstanding RECEIVEs and SEND should receive "reset" responses,
        //   all segment queues should be flushed, the user should also
        //   receive an unsolicited general "connection reset" signal, enter
        //   the CLOSED state, delete the TCB, and return.
        //   If the SYN is not in the window this step would not be reached
        //   and an ack would have been sent in the first step (sequence
        //   number check).
        if contents.control() == Some(Control::SYN) {
            return (
                Some(Closed { reason: UserError::ConnectionReset }),
                Some(Segment::rst(snd.nxt)),
            );
        }
        match ack {
            Some(ack) => {
                if ack.after(snd.nxt) {
                    //   If the ACK acks something not yet sent (SEG.ACK >
                    //   SND.NXT) then send an ACK, drop the segment, and
                    //   return.
                    return (None, Some(Segment::ack(snd.nxt, rcv.nxt(), rcv.wnd()).into()));
                } else {
                    // TODO(https://fxbug.dev/91315): Update send states here.
                    // 1. update the window size.
                    // 2. reclaim space in the send buffer for acked data.
                    // 3. form a new segment to send if possible.
                    //   If the ACK is a duplicate (SEG.ACK < SND.UNA), it can be
                    //   ignored.
                }
            }
            //   if the ACK bit is off drop the segment and return.
            None => return (None, None),
        }
        //   Once in the ESTABLISHED state, it is possible to deliver segment
        //   text to user RECEIVE buffers.  Text from segments can be moved
        //   into buffers until either the buffer is full or the segment is
        //   empty.  If the segment empties and carries an PUSH flag, then
        //   the user is informed, when the buffer is returned, that a PUSH
        //   has been received.
        //
        //   When the TCP takes responsibility for delivering the data to the
        //   user it must also acknowledge the receipt of the data.
        //   Once the TCP takes responsibility for the data it advances
        //   RCV.NXT over the data accepted, and adjusts RCV.WND as
        //   apporopriate to the current buffer availability.  The total of
        //   RCV.NXT and RCV.WND should not be reduced.
        //
        //   Please note the window management suggestions in section 3.7.
        //   Send an acknowledgment of the form:
        //     <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
        //   This acknowledgment should be piggybacked on a segment being
        //   transmitted if possible without incurring undue delay.
        if contents.data().len() > 0 {
            let offset = usize::try_from(seq - rcv.nxt()).unwrap_or_else(|TryFromIntError {..}| {
                panic!("The segment was trimmed to fit the window, thus seg.seq({:?}) must not come before rcv.nxt({:?})", seq, rcv.nxt());
            });
            // Write the segment data in the buffer and keep track if it fills
            // any hole in the assembler.
            let nwritten = rcv.buffer.write_at(offset, contents.data());
            let readable = rcv.assembler.insert(seq..seq + nwritten);
            rcv.buffer.make_readable(readable);
        }
        // TODO(https://fxbug.dev/93522): Handle FIN.
        (None, Some(Segment::ack(snd.nxt, rcv.nxt(), rcv.wnd())))
    }
}

#[derive(Debug)]
enum State<R: ReceiveBuffer> {
    Closed(Closed<UserError>),
    Listen(Listen),
    SynRcvd(SynRcvd),
    SynSent(SynSent),
    Established(Established<R>),
}

impl<R: ReceiveBuffer> State<R> {
    /// Processes an incoming segment and advances the state machine.
    fn on_segment<P: Payload>(&mut self, incoming: Segment<P>) -> Option<Segment<()>> {
        let (maybe_new_state, seg) = match self {
            State::SynRcvd(synrcvd) => {
                let (maybe_new_state, seg) = synrcvd.on_segment(incoming);
                (maybe_new_state.map(State::from), seg)
            }
            State::Established(established) => {
                let (maybe_new_state, seg) = established.on_segment(incoming);
                (maybe_new_state.map(State::from), seg)
            }
            State::SynSent(synsent) => {
                let (maybe_new_state, seg) = synsent.on_segment(incoming);
                (maybe_new_state.map(State::from), seg)
            }
            State::Closed(closed) => (None, closed.on_segment(incoming)),
            State::Listen(listen) => {
                let (maybe_syn_rcvd, seg) = listen.on_segment(incoming);
                (maybe_syn_rcvd.map(State::SynRcvd), seg)
            }
        };
        if let Some(new_state) = maybe_new_state {
            *self = new_state;
        }
        seg
    }
}

/// One of two possible states which result from a state transition.
#[cfg_attr(test, derive(Debug, PartialEq, Eq))]
enum Either<A, B> {
    A(A),
    B(B),
}

impl<A, B, R> From<Either<A, B>> for State<R>
where
    R: ReceiveBuffer,
    A: Into<State<R>>,
    B: Into<State<R>>,
{
    fn from(or: Either<A, B>) -> Self {
        match or {
            Either::A(a) => a.into(),
            Either::B(b) => b.into(),
        }
    }
}

impl<R: ReceiveBuffer> From<Closed<UserError>> for State<R> {
    fn from(closed: Closed<UserError>) -> Self {
        State::Closed(closed)
    }
}

impl<R: ReceiveBuffer> From<Listen> for State<R> {
    fn from(listen: Listen) -> Self {
        State::Listen(listen)
    }
}

impl<R: ReceiveBuffer> From<SynRcvd> for State<R> {
    fn from(synrcvd: SynRcvd) -> Self {
        State::SynRcvd(synrcvd)
    }
}

impl<R: ReceiveBuffer> From<SynSent> for State<R> {
    fn from(synsent: SynSent) -> Self {
        State::SynSent(synsent)
    }
}

impl<R: ReceiveBuffer> From<Established<R>> for State<R> {
    fn from(established: Established<R>) -> Self {
        State::Established(established)
    }
}

#[cfg(test)]
mod test {
    use core::num::NonZeroUsize;

    use assert_matches::assert_matches;
    use test_case::test_case;

    use super::*;
    use crate::transport::tcp::buffer::{Buffer, RingBuffer};

    const ISS_1: SeqNum = SeqNum::new(100);
    const ISS_2: SeqNum = SeqNum::new(300);

    impl Segment<&[u8]> {
        fn data(seq: SeqNum, ack: SeqNum, wnd: WindowSize, data: &[u8]) -> Segment<&[u8]> {
            let (seg, truncated) = Segment::with_data(seq, Some(ack), None, wnd, data);
            assert_eq!(truncated, 0);
            seg
        }
    }

    /// A buffer that can't read or write for test purpose.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    struct NullBuffer;

    impl Buffer for NullBuffer {
        fn len(&self) -> usize {
            0
        }

        fn cap(&self) -> usize {
            0
        }
    }

    impl ReceiveBuffer for NullBuffer {
        fn write_at<P: Payload>(&mut self, _offset: usize, _data: &P) -> usize {
            0
        }

        fn make_readable(&mut self, count: usize) {
            assert_eq!(count, 0);
        }

        fn read_with<'a, F>(&'a mut self, f: F) -> usize
        where
            F: for<'b> FnOnce(&'b [&'a [u8]]) -> usize,
        {
            assert_eq!(f(&[]), 0);
            0
        }
    }

    #[test_case(Segment::rst(ISS_1) => None; "drop RST")]
    #[test_case(Segment::rst_ack(ISS_1, ISS_2) => None; "drop RST|ACK")]
    #[test_case(Segment::syn(ISS_1, WindowSize::ZERO) => Some(Segment::rst_ack(SeqNum::new(0), ISS_1 + 1)); "reset SYN")]
    #[test_case(Segment::syn_ack(ISS_1, ISS_2, WindowSize::ZERO) => Some(Segment::rst(ISS_2)); "reset SYN|ACK")]
    #[test_case(Segment::data(ISS_1, ISS_2, WindowSize::ZERO, &[0, 1, 2][..]) => Some(Segment::rst(ISS_2)); "reset data segment")]
    fn segment_arrives_when_closed(
        incoming: impl Into<Segment<&'static [u8]>>,
    ) -> Option<Segment<()>> {
        let closed = Closed { reason: () };
        closed.on_segment(incoming.into())
    }

    #[test_case(Segment::rst_ack(ISS_2, ISS_1 - 1) => (None, None); "unacceptable ACK with RST")]
    #[test_case(
        Segment::ack(ISS_2, ISS_1 - 1, WindowSize::DEFAULT)
    => (Some(Either::B(Closed {
        reason: UserError::ConnectionReset,
    })), Some(
        Segment::rst(ISS_1-1)
    )); "unacceptable ACK without RST")]
    #[test_case(
        Segment::rst_ack(ISS_2, ISS_1)
    => (Some(Either::B(Closed {
        reason: UserError::ConnectionReset,
    })), None); "acceptable ACK(ISS) with RST")]
    #[test_case(
        Segment::rst_ack(ISS_2, ISS_1 + 1)
    => (Some(Either::B(Closed {
        reason: UserError::ConnectionReset,
    })), None); "acceptable ACK(ISS+1) with RST")]
    #[test_case(
        Segment::rst(ISS_2)
    => (None, None); "RST without ack")]
    #[test_case(
        Segment::syn(ISS_2, WindowSize::DEFAULT)
    => (Some(Either::A(Either::B( SynRcvd {
        iss: ISS_1,
        irs: ISS_2,
    }))), Some(Segment::syn_ack(ISS_1, ISS_2 + 1, WindowSize::DEFAULT))); "SYN only")]
    #[test_case(
        Segment::fin(ISS_2, ISS_1 + 1, WindowSize::DEFAULT)
    => (None, None); "acceptable ACK with FIN")]
    #[test_case(
        Segment::ack(ISS_2, ISS_1 + 1, WindowSize::DEFAULT)
    => (None, None); "acceptable ACK(ISS+1) with nothing")]
    #[test_case(
        Segment::ack(ISS_2, ISS_1, WindowSize::DEFAULT)
    => (None, None); "acceptable ACK(ISS) without RST")]
    fn segment_arrives_when_syn_sent(
        incoming: Segment<()>,
    ) -> (
        Option<Either<Either<Established<NullBuffer>, SynRcvd>, Closed<UserError>>>,
        Option<Segment<()>>,
    ) {
        let syn_sent = SynSent { iss: ISS_1 };
        syn_sent.on_segment(incoming)
    }

    #[test_case(Segment::rst(ISS_2) => (None, None); "ignore RST")]
    #[test_case(Segment::ack(ISS_2, ISS_1, WindowSize::DEFAULT) => (None, Some(Segment::rst(ISS_1))); "reject ACK")]
    #[test_case(Segment::syn(ISS_2, WindowSize::DEFAULT) => (Some(SynRcvd {
        iss: ISS_1,
        irs: ISS_2,
    }), Some(Segment::syn_ack(ISS_1, ISS_2 + 1, WindowSize::DEFAULT))); "accept syn")]
    fn segment_arrives_when_listen(
        incoming: Segment<()>,
    ) -> (Option<SynRcvd>, Option<Segment<()>>) {
        let listen = Closed::listen(ISS_1);
        listen.on_segment(incoming)
    }

    #[test_case(Segment::ack(ISS_1, ISS_2, WindowSize::DEFAULT)
    => (None, Some(Segment::ack(ISS_2 + 1, ISS_1 + 1, WindowSize::DEFAULT))); "OTW segment")]
    #[test_case(Segment::rst_ack(ISS_1, ISS_2)
    => (None, None); "OTW RST")]
    #[test_case(Segment::rst_ack(ISS_1 + 1, ISS_2)
    => (
        Some(Either::B(Closed { reason: UserError::ConnectionReset })),
        None
    ); "acceptable RST")]
    #[test_case(Segment::syn(ISS_1 + 1, WindowSize::DEFAULT)
    => (
        Some(Either::B(Closed { reason: UserError::ConnectionReset })),
        Some(Segment::rst_ack(ISS_2, ISS_1))
    ); "duplicate syn")]
    #[test_case(Segment::ack(ISS_1 + 1, ISS_2, WindowSize::DEFAULT)
    => (
        None,
        Some(Segment::rst(ISS_2)),
    ); "unacceptable ack (ISS)")]
    #[test_case(Segment::ack(ISS_1 + 1, ISS_2 + 1, WindowSize::DEFAULT)
    => (
        Some(Either::A(Either::A(Established {
            snd: Send { nxt: ISS_2 + 1, una: ISS_2 + 1, wnd: WindowSize::DEFAULT },
            rcv: Recv { buffer: NullBuffer, assembler: Assembler::new(ISS_1 + 1) },
        }))),
        None,
    ); "acceptable ack (ISS + 1)")]
    #[test_case(Segment::ack(ISS_1 + 1, ISS_2 + 2, WindowSize::DEFAULT)
    => (
        None,
        Some(Segment::rst(ISS_2 + 2))
    ); "unacceptable ack (ISS + 2)")]
    #[test_case(Segment::ack(ISS_1 + 1, ISS_2 - 1, WindowSize::DEFAULT)
    => (
        None,
        Some(Segment::rst(ISS_2 - 1))
    ); "unacceptable ack (ISS - 1)")]
    #[test_case(Segment::new(ISS_1 + 1, None, None, WindowSize::DEFAULT)
    => (None, None); "no ack")]
    fn segment_arrives_when_syn_rcvd(
        incoming: Segment<()>,
    ) -> (
        Option<Either<Either<Established<NullBuffer>, Listen>, Closed<UserError>>>,
        Option<Segment<()>>,
    ) {
        let syn_rcvd = SynRcvd { iss: ISS_2, irs: ISS_1 };
        syn_rcvd.on_segment(incoming)
    }

    #[test_case(Segment::syn(ISS_2 + 1, WindowSize::DEFAULT)
        => (Some(Closed { reason: UserError::ConnectionReset }), Some(Segment::rst(ISS_1 + 1))))]
    #[test_case(Segment::rst(ISS_2 + 1)
        => (Some(Closed { reason: UserError::ConnectionReset }), None))]
    fn segment_arrives_when_established(
        incoming: Segment<()>,
    ) -> (Option<Closed<UserError>>, Option<Segment<()>>) {
        let mut established = Established {
            snd: Send { nxt: ISS_1 + 1, una: ISS_1 + 1, wnd: WindowSize::DEFAULT },
            rcv: Recv {
                buffer: RingBuffer::new(NonZeroUsize::new(1).unwrap()),
                assembler: Assembler::new(ISS_2 + 1),
            },
        };
        established.on_segment(incoming)
    }

    #[test]
    fn active_passive_open() {
        let (syn_sent, syn_seg) = Closed::connect(ISS_1);
        assert_eq!(syn_seg, Segment::syn(ISS_1, WindowSize::DEFAULT));
        assert_eq!(syn_sent, SynSent { iss: ISS_1 });
        let mut active = State::SynSent(syn_sent);
        let mut passive = State::Listen(Closed::listen(ISS_2));
        let syn_ack = passive.on_segment(syn_seg).expect("failed to generate a syn-ack segment");
        assert_eq!(syn_ack, Segment::syn_ack(ISS_2, ISS_1 + 1, WindowSize::DEFAULT));
        assert_matches!(passive, State::SynRcvd(ref syn_rcvd) if syn_rcvd == &SynRcvd {
            iss: ISS_2,
            irs: ISS_1,
        });
        let ack_seg = active.on_segment(syn_ack).expect("failed to generate a ack segment");
        assert_eq!(ack_seg, Segment::ack(ISS_1 + 1, ISS_2 + 1, WindowSize::ZERO));
        assert_matches!(active, State::Established(established) if established == Established {
            snd: Send {
                nxt: ISS_1 + 1,
                una: ISS_1 + 1,
                wnd: WindowSize::DEFAULT,
            },
            rcv: Recv { buffer: NullBuffer, assembler: Assembler::new(ISS_2 + 1) }
        });
        assert_eq!(passive.on_segment(ack_seg), None);
        assert_matches!(passive, State::Established(established) if established == Established {
            snd: Send {
                nxt: ISS_2 + 1,
                una: ISS_2 + 1,
                wnd: WindowSize::DEFAULT,
            },
            rcv: Recv { buffer: NullBuffer, assembler: Assembler::new(ISS_1 + 1) }
        })
    }

    #[test]
    fn simultaneous_open() {
        let (syn_sent1, syn1) = Closed::connect(ISS_1);
        let (syn_sent2, syn2) = Closed::connect(ISS_2);

        assert_eq!(syn1, Segment::syn(ISS_1, WindowSize::DEFAULT));
        assert_eq!(syn2, Segment::syn(ISS_2, WindowSize::DEFAULT));

        let mut state1 = State::SynSent(syn_sent1);
        let mut state2 = State::SynSent(syn_sent2);

        let syn_ack1 = state1.on_segment(syn2).expect("failed to generate syn ack");
        let syn_ack2 = state2.on_segment(syn1).expect("failed to generate syn ack");

        assert_eq!(syn_ack1, Segment::syn_ack(ISS_1, ISS_2 + 1, WindowSize::DEFAULT));
        assert_eq!(syn_ack2, Segment::syn_ack(ISS_2, ISS_1 + 1, WindowSize::DEFAULT));

        assert_matches!(state1, State::SynRcvd(ref syn_rcvd) if syn_rcvd == &SynRcvd {
            iss: ISS_1,
            irs: ISS_2,
        });
        assert_matches!(state2, State::SynRcvd(ref syn_rcvd) if syn_rcvd == &SynRcvd {
            iss: ISS_2,
            irs: ISS_1,
        });

        assert_eq!(state1.on_segment(syn_ack2), None);
        assert_eq!(state2.on_segment(syn_ack1), None);

        assert_matches!(state1, State::Established(established) if established == Established {
            snd: Send {
                nxt: ISS_1 + 1,
                una: ISS_1 + 1,
                wnd: WindowSize::DEFAULT,
            },
            rcv: Recv {
                buffer: NullBuffer,
                assembler: Assembler::new(ISS_2 + 1),
            }
        });

        assert_matches!(state2, State::Established(established) if established == Established {
            snd: Send {
                nxt: ISS_2 + 1,
                una: ISS_2 + 1,
                wnd: WindowSize::DEFAULT,
            },
            rcv: Recv {
                buffer: NullBuffer,
                assembler: Assembler::new(ISS_1 + 1),
            }
        });
    }

    #[test]
    fn established_receive() {
        const BUFFER_SIZE: usize = 16;
        const BUFFER_SIZE_U32: u32 = BUFFER_SIZE as u32;
        let mut established = Established {
            snd: Send { nxt: ISS_1 + 1, una: ISS_1 + 1, wnd: WindowSize::ZERO },
            rcv: Recv {
                buffer: RingBuffer::new(NonZeroUsize::new(BUFFER_SIZE).unwrap()),
                assembler: Assembler::new(ISS_2 + 1),
            },
        };

        const TEST_BYTES: &[u8] = "Hello".as_bytes();
        const TEST_BYTES_LEN: u32 = TEST_BYTES.len() as u32;

        // Received an expected segment at rcv.nxt.
        assert_eq!(
            established.on_segment(Segment::data(
                ISS_2 + 1,
                ISS_1 + 1,
                WindowSize::ZERO,
                TEST_BYTES,
            )),
            (
                None,
                Some(Segment::ack(
                    ISS_1 + 1,
                    ISS_2 + 1 + TEST_BYTES.len(),
                    WindowSize::new(BUFFER_SIZE_U32 - TEST_BYTES_LEN).unwrap()
                )),
            )
        );
        assert_eq!(
            established.rcv.buffer.read_with(|available| {
                assert_eq!(available, &[TEST_BYTES]);
                available[0].len()
            }),
            TEST_BYTES.len()
        );

        // Receive an out-of-order segment.
        assert_eq!(
            established.on_segment(Segment::data(
                ISS_2 + 1 + TEST_BYTES.len() * 2,
                ISS_1 + 1,
                WindowSize::ZERO,
                TEST_BYTES,
            )),
            (
                None,
                Some(Segment::ack(
                    ISS_1 + 1,
                    ISS_2 + 1 + TEST_BYTES.len(),
                    WindowSize::new(BUFFER_SIZE_U32).unwrap()
                )),
            )
        );
        assert_eq!(
            established.rcv.buffer.read_with(|available| {
                assert_eq!(available, &[&[][..]]);
                0
            }),
            0
        );

        // Receive the next segment that fills the hole.
        assert_eq!(
            established.on_segment(Segment::data(
                ISS_2 + 1 + TEST_BYTES.len(),
                ISS_1 + 1,
                WindowSize::ZERO,
                TEST_BYTES,
            )),
            (
                None,
                Some(Segment::ack(
                    ISS_1 + 1,
                    ISS_2 + 1 + 3 * TEST_BYTES.len(),
                    WindowSize::new(BUFFER_SIZE_U32 - 2 * TEST_BYTES_LEN).unwrap()
                )),
            )
        );
        assert_eq!(
            established.rcv.buffer.read_with(|available| {
                assert_eq!(available, &[[TEST_BYTES, TEST_BYTES].concat()]);
                available[0].len()
            }),
            10
        );
    }
}
