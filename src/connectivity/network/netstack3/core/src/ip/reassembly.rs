// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP fragmented packet reassembly support.
//!
//! `reassembly` is a utility to support reassembly of fragmented IP packets.
//! Fragmented packets are associated by a combination of the packets' source
//! address, destination address and identification value. When a potentially
//! fragmented packet is received, this utility will check to see if the packet
//! is in fact fragmented or not. If it isn't fragmented, it will be returned as
//! is without any modification. If it is fragmented, this utility will capture
//! its body and store it in a cache while waiting for all the fragments for a
//! packet to arrive. The header information from a fragment with offset set to
//! 0 will also be kept to add to the final, reassembled packet. Once this
//! utility has received all the fragments for a combination of source address,
//! destination address and identification value, the implementer will need to
//! allocate a buffer of sufficient size to reassemble the final packet into and
//! pass it to this utility. This utility will then attempt to reassemble and
//! parse the packet, which will be returned to the caller. The caller should
//! then handle the returned packet as a normal IP packet. Note, there is a
//! timer from receipt of the first fragment to reassembly of the final packet.
//! See [`REASSEMBLY_TIMEOUT_SECONDS`].
//!
//! Note, this utility does not support reassembly of jumbogram packets.
//! According to the IPv6 Jumbogram RFC (RFC 2675), the jumbogram payload option
//! is relevant only for nodes that may be attached to links with a link MTU
//! greater than 65575 bytes. Note, the maximum size of a non-jumbogram IPv6
//! packet is also 65575 (as the payload length field for IP packets is 16 bits
//! + 40 byte IPv6 header). If a link supports an MTU greater than the maximum
//! size of a non-jumbogram packet, the packet should not be fragmented.

use alloc::{
    collections::{
        hash_map::{Entry, HashMap},
        BTreeSet, BinaryHeap,
    },
    vec::Vec,
};
use core::{cmp::Ordering, convert::TryFrom, marker::PhantomData, time::Duration};

use assert_matches::assert_matches;
use net_types::ip::{Ip, IpAddr, IpAddress};
use packet::BufferViewMut;
use packet_formats::{
    ip::IpPacket,
    ipv4::{Ipv4Header, Ipv4Packet},
    ipv6::{ext_hdrs::Ipv6ExtensionHeaderData, Ipv6Packet},
};
use zerocopy::{ByteSlice, ByteSliceMut};

use crate::{
    context::{TimerContext, TimerHandler},
    ip::IpExt,
};

/// The maximum amount of time from receipt of the first fragment to reassembly
/// of a packet. Note, "first fragment" does not mean a fragment with offset 0;
/// it means the first fragment packet we receive with a new combination of
/// source address, destination address and fragment identification value.
const REASSEMBLY_TIMEOUT: Duration = Duration::from_secs(60);

/// Number of bytes per fragment block for IPv4 and IPv6.
///
/// IPv4 outlines the fragment block size in RFC 791 section 3.1, under the
/// fragment offset field's description: "The fragment offset is measured in
/// units of 8 octets (64 bits)".
///
/// IPv6 outlines the fragment block size in RFC 8200 section 4.5, under the
/// fragment offset field's description: "The offset, in 8-octet units, of the
/// data following this header".
const FRAGMENT_BLOCK_SIZE: u8 = 8;

/// Maximum number of fragment blocks an IPv4 or IPv6 packet can have.
///
/// We use this value because both IPv4 fixed header's fragment offset field and
/// IPv6 fragment extension header's fragment offset field are 13 bits wide.
const MAX_FRAGMENT_BLOCKS: u16 = 8191;

/// Maximum number of bytes of all currently cached fragments per IP protocol.
///
/// If the current cache size is less than this number, a new fragment can be
/// cached (even if this will result in the total cache size exceeding this
/// threshold). If the current cache size >= this number, the incoming fragment
/// will be dropped.
const MAX_FRAGMENT_CACHE_SIZE: usize = 4 * 1024 * 1024;

/// The state context for the fragment cache.
pub(super) trait FragmentStateContext<I: Ip, Instant> {
    /// Returns a mutable reference to the fragment cache.
    fn with_state_mut<O, F: FnOnce(&mut IpPacketFragmentCache<I, Instant>) -> O>(
        &mut self,
        cb: F,
    ) -> O;
}

/// The non-synchronized execution context for IP packet fragment reassembly.
trait FragmentNonSyncContext<I: Ip>: TimerContext<FragmentCacheKey<I::Addr>> {}
impl<I: Ip, C: TimerContext<FragmentCacheKey<I::Addr>>> FragmentNonSyncContext<I> for C {}

/// The execution context for IP packet fragment reassembly.
trait FragmentContext<I: Ip, C: FragmentNonSyncContext<I>>:
    FragmentStateContext<I, C::Instant>
{
}

impl<I: Ip, C: FragmentNonSyncContext<I>, SC: FragmentStateContext<I, C::Instant>>
    FragmentContext<I, C> for SC
{
}

/// An implementation of a fragment cache.
pub(crate) trait FragmentHandler<I: IpExt, C> {
    /// Attempts to process a packet fragment.
    ///
    /// # Panics
    ///
    /// Panics if the packet has no fragment data.
    fn process_fragment<B: ByteSlice>(
        &mut self,
        ctx: &mut C,
        packet: I::Packet<B>,
    ) -> FragmentProcessingState<I, B>
    where
        I::Packet<B>: FragmentablePacket;

    /// Attempts to reassemble a packet.
    ///
    /// Attempts to reassemble a packet associated with a given
    /// `FragmentCacheKey`, `key`, and cancels the timer to reset reassembly
    /// data. The caller is expected to allocate a buffer of sufficient size
    /// (available from `process_fragment` when it returns a
    /// `FragmentProcessingState::Ready` value) and provide it to
    /// `reassemble_packet` as `buffer` where the packet will be reassembled
    /// into.
    ///
    /// # Panics
    ///
    /// Panics if the provided `buffer` does not have enough capacity for the
    /// reassembled packet. Also panics if a different `ctx` is passed to
    /// `reassemble_packet` from the one passed to `process_fragment` when
    /// processing a packet with a given `key` as `reassemble_packet` will fail
    /// to cancel the reassembly timer.
    fn reassemble_packet<B: ByteSliceMut, BV: BufferViewMut<B>>(
        &mut self,
        ctx: &mut C,
        key: &FragmentCacheKey<I::Addr>,
        buffer: BV,
    ) -> Result<I::Packet<B>, FragmentReassemblyError>;
}

impl<I: IpExt, C: FragmentNonSyncContext<I>, SC: FragmentContext<I, C>> FragmentHandler<I, C>
    for SC
{
    fn process_fragment<B: ByteSlice>(
        &mut self,
        ctx: &mut C,
        packet: I::Packet<B>,
    ) -> FragmentProcessingState<I, B>
    where
        I::Packet<B>: FragmentablePacket,
    {
        self.with_state_mut(|cache| {
            let (res, timer_id) = cache.process_fragment(packet);

            if let Some(timer_id) = timer_id {
                match timer_id {
                    CacheTimerAction::CreateNewTimer(timer_id) => {
                        assert_eq!(ctx.schedule_timer(REASSEMBLY_TIMEOUT, timer_id), None)
                    }
                    CacheTimerAction::CancelExistingTimer(timer_id) => {
                        assert_ne!(ctx.cancel_timer(timer_id), None)
                    }
                }
            }

            res
        })
    }

    fn reassemble_packet<B: ByteSliceMut, BV: BufferViewMut<B>>(
        &mut self,
        ctx: &mut C,
        key: &FragmentCacheKey<I::Addr>,
        buffer: BV,
    ) -> Result<I::Packet<B>, FragmentReassemblyError> {
        self.with_state_mut(|cache| {
            let res = cache.reassemble_packet(key, buffer);

            match res {
                Ok(_) | Err(FragmentReassemblyError::PacketParsingError) => {
                    // Cancel the reassembly timer as we attempt reassembly which
                    // means we had all the fragments for the final packet, even
                    // if parsing the reassembled packet failed.
                    assert_matches!(ctx.cancel_timer(*key), Some(_));
                }
                Err(FragmentReassemblyError::InvalidKey)
                | Err(FragmentReassemblyError::MissingFragments) => {}
            }

            res
        })
    }
}

impl<A: IpAddress, C: FragmentNonSyncContext<A::Version>, SC: FragmentContext<A::Version, C>>
    TimerHandler<C, FragmentCacheKey<A>> for SC
where
    A::Version: IpExt,
{
    fn handle_timer(&mut self, _ctx: &mut C, key: FragmentCacheKey<A>) {
        // If a timer fired, the `key` must still exist in our fragment cache.
        assert_matches!(self.with_state_mut(|cache| cache.remove_data(&key)), Some(_));
    }
}

/// Trait that must be implemented by any packet type that is fragmentable.
pub(crate) trait FragmentablePacket {
    /// Return fragment identifier data.
    ///
    /// Returns the fragment identification, offset and more flag as `(a, b, c)`
    /// where `a` is the fragment identification value, `b` is the fragment
    /// offset and `c` is the more flag.
    ///
    /// # Panics
    ///
    /// Panics if the packet has no fragment data.
    fn fragment_data(&self) -> (u32, u16, bool);
}

impl<B: ByteSlice> FragmentablePacket for Ipv4Packet<B> {
    fn fragment_data(&self) -> (u32, u16, bool) {
        (u32::from(self.id()), self.fragment_offset(), self.mf_flag())
    }
}

impl<B: ByteSlice> FragmentablePacket for Ipv6Packet<B> {
    fn fragment_data(&self) -> (u32, u16, bool) {
        for ext_hdr in self.iter_extension_hdrs() {
            if let Ipv6ExtensionHeaderData::Fragment { fragment_data } = ext_hdr.data() {
                return (
                    fragment_data.identification(),
                    fragment_data.fragment_offset(),
                    fragment_data.m_flag(),
                );
            }
        }

        unreachable!(
            "Should never call this function if the packet does not have a fragment header"
        );
    }
}

/// Possible return values for [`IpPacketFragmentCache::process_fragment`].
#[derive(Debug)]
pub(crate) enum FragmentProcessingState<I: IpExt, B: ByteSlice> {
    /// The provided packet is not fragmented so no processing is required.
    /// The packet is returned with this value without any modification.
    NotNeeded(I::Packet<B>),

    /// The provided packet is fragmented but it is malformed.
    ///
    /// Possible reasons for being malformed are:
    ///  1) Body is not a multiple of `FRAGMENT_BLOCK_SIZE` and  it is not the
    ///     last fragment (last fragment of a packet, not last fragment received
    ///     for a packet).
    ///  2) Overlaps with an existing fragment. This is explicitly not allowed
    ///     for IPv6 as per RFC 8200 section 4.5 (more details in RFC 5722). We
    ///     choose the same behaviour for IPv4 for the same reasons.
    ///  3) Packet's fragment offset + # of fragment blocks >
    ///     `MAX_FRAGMENT_BLOCKS`.
    // TODO(ghanan): Investigate whether disallowing overlapping fragments for
    //               IPv4 cause issues interoperating with hosts that produce
    //               overlapping fragments.
    InvalidFragment,

    /// Successfully processed the provided fragment. We are still waiting on
    /// more fragments for a packet to arrive before being ready to reassemble
    /// the packet.
    NeedMoreFragments,

    /// Cannot process the fragment because `MAX_FRAGMENT_CACHE_SIZE` is
    /// reached.
    OutOfMemory,

    /// Successfully processed the provided fragment. We now have all the
    /// fragments we need to reassemble the packet. The caller must create a
    /// buffer with capacity for at least `packet_len` bytes and provide the
    /// buffer and `key` to `reassemble_packet`.
    Ready { key: FragmentCacheKey<I::Addr>, packet_len: usize },
}

/// Possible errors when attempting to reassemble a packet.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum FragmentReassemblyError {
    /// At least one fragment for a packet has not arrived.
    MissingFragments,

    /// A `FragmentCacheKey` is not associated with any packet. This could be
    /// because either no fragment has yet arrived for a packet associated with
    /// a `FragmentCacheKey` or some fragments did arrive, but the reassembly
    /// timer expired and got discarded.
    InvalidKey,

    /// Packet parsing error.
    PacketParsingError,
}

/// Fragment Cache Key.
///
/// Composed of the original packet's source address, destination address,
/// and fragment id.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub(crate) struct FragmentCacheKey<A: IpAddress>(A, A, u32);

impl<A: IpAddress> FragmentCacheKey<A> {
    pub(crate) fn new(src_ip: A, dst_ip: A, fragment_id: u32) -> Self {
        FragmentCacheKey(src_ip, dst_ip, fragment_id)
    }
}

/// An inclusive-inclusive range of bytes within a reassembled packet.
// NOTE: We use this instead of `std::ops::RangeInclusive` because the latter
// provides getter methods which return references, and it adds a lot of
// unnecessary dereferences.
#[derive(Copy, Clone, Debug, Eq, PartialEq, PartialOrd, Ord)]
struct BlockRange {
    start: u16,
    end: u16,
}

/// Data required for fragmented packet reassembly.
#[derive(Debug)]
struct FragmentCacheData {
    /// List of non-overlapping inclusive ranges of fragment blocks required
    /// before being ready to reassemble a packet.
    ///
    /// When creating a new instance of `FragmentCacheData`, we will set
    /// `missing_blocks` to a list with a single element representing all
    /// blocks, (0, MAX_VALUE). In this case, MAX_VALUE will be set to
    /// `core::u16::MAX`.
    // TODO(ghanan): Consider a different data structure? With the BTreeSet,
    //               searches will be O(n) and inserts/removals may be
    //               O(log(n)). If we use a linked list, searches will be O(n)
    //               but inserts/removals will be O(1). For now, a `BTreeSet` is
    //               used since Rust provides one and it does the job of keeping
    //               the list of gaps in increasing order when searching,
    //               inserting and removing. How many fragments for a packet
    //               will we get in practice though?
    // TODO(fxbug.dev/50830): O(n) complexity per fragment is a DDOS
    //              vulnerability: this should be refactored to be O(log(n)).
    missing_blocks: BTreeSet<BlockRange>,

    /// Received fragment blocks.
    ///
    /// We use a binary heap for help when reassembling packets. When we
    /// reassemble packets, we will want to fill up a new buffer with all the
    /// body fragments. The easiest way to do this is in order, from the
    /// fragment with offset 0 to the fragment with the highest offset. Since we
    /// only need to enforce the order when reassembling, we use a min-heap so
    /// we have a defined order (increasing fragment offset values) when
    /// popping. `BinaryHeap` is technically a max-heap, but we use the negative
    /// of the offset values as the key for the heap. See
    /// [`PacketBodyFragment::new`].
    body_fragments: BinaryHeap<PacketBodyFragment>,

    /// The header data for the reassembled packet.
    ///
    /// The header of the fragment packet with offset 0 will be used as the
    /// header for the final, reassembled packet.
    header: Option<Vec<u8>>,

    /// Total number of bytes in the reassembled packet.
    ///
    /// This is used so that we don't have to iterated through `body_fragments`
    /// and sum the partial body sizes to calculate the reassembled packet's
    /// size.
    total_size: usize,
}

impl Default for FragmentCacheData {
    fn default() -> FragmentCacheData {
        FragmentCacheData {
            missing_blocks: core::iter::once(BlockRange { start: 0, end: u16::MAX }).collect(),
            body_fragments: BinaryHeap::new(),
            header: None,
            total_size: 0,
        }
    }
}

impl FragmentCacheData {
    /// Attempts to find a gap where `fragment_blocks_range` will fit in.
    ///
    /// Returns `Some(o)` if a valid gap is found where `o` is the gap's offset
    /// range; otherwise, returns `None`. `fragment_blocks_range` is an
    /// inclusive range of fragment block offsets.
    fn find_gap(&self, fragment_blocks_range: BlockRange) -> Option<BlockRange> {
        for potential_gap in self.missing_blocks.iter() {
            if fragment_blocks_range.end < potential_gap.start
                || fragment_blocks_range.start > potential_gap.end
            {
                // Either:
                // - Our packet's ending offset is less than the start of
                //   `potential_gap` so move on to the next gap. That is,
                //   `fragment_blocks_range` ends before `potential_gap`.
                // - Our packet's starting offset is more than `potential_gap`'s
                //   ending offset so move on to the next gap. That is,
                //   `fragment_blocks_range` starts after `potential_gap`.
                continue;
            }

            // Make sure that `fragment_blocks_range` belongs purely within
            // `potential_gap`.
            //
            // If `fragment_blocks_range` does not fit purely within
            // `potential_gap`, then at least one block in
            // `fragment_blocks_range` overlaps with an already received block.
            // We should never receive overlapping fragments from non-malicious
            // nodes.
            if (fragment_blocks_range.start < potential_gap.start)
                || (fragment_blocks_range.end > potential_gap.end)
            {
                break;
            }

            // Found a gap where `fragment_blocks_range` fits in!
            return Some(*potential_gap);
        }

        // Unable to find a valid gap so return `None`.
        None
    }
}

/// A cache of inbound IP packet fragments.
#[derive(Debug)]
pub(crate) struct IpPacketFragmentCache<I: Ip, Instant> {
    cache: HashMap<FragmentCacheKey<I::Addr>, FragmentCacheData>,
    size: usize,
    threshold: usize,
    _marker: PhantomData<Instant>,
}

impl<I: Ip, Instant> Default for IpPacketFragmentCache<I, Instant> {
    fn default() -> IpPacketFragmentCache<I, Instant> {
        IpPacketFragmentCache {
            cache: HashMap::new(),
            size: 0,
            threshold: MAX_FRAGMENT_CACHE_SIZE,
            _marker: PhantomData,
        }
    }
}

enum CacheTimerAction<A: IpAddress> {
    CreateNewTimer(FragmentCacheKey<A>),
    CancelExistingTimer(FragmentCacheKey<A>),
}

// TODO(https://fxbug.dev/92672): Make these operate on a context trait rather
// than `&self` and `&mut self`.
impl<I: IpExt, Instant> IpPacketFragmentCache<I, Instant> {
    /// Attempts to process a packet fragment.
    ///
    /// # Panics
    ///
    /// Panics if the packet has no fragment data.
    fn process_fragment<B: ByteSlice>(
        &mut self,
        packet: I::Packet<B>,
    ) -> (FragmentProcessingState<I, B>, Option<CacheTimerAction<I::Addr>>)
    where
        I::Packet<B>: FragmentablePacket,
    {
        if self.above_size_threshold() {
            return (FragmentProcessingState::OutOfMemory, None);
        }

        // Get the fragment data.
        let (id, offset, m_flag) = packet.fragment_data();

        // Check if `packet` is actually fragmented. We know it is not
        // fragmented if the fragment offset is 0 (contains first fragment) and
        // we have no more fragments. This means the first fragment is the only
        // fragment, implying we have a full packet.
        if offset == 0 && !m_flag {
            return (FragmentProcessingState::NotNeeded(packet), None);
        }

        // Make sure packet's body isn't empty. Since at this point we know that
        // the packet is definitely fragmented (`offset` is not 0 or `m_flag` is
        // `true`), we simply let the caller know we need more fragments. This
        // should never happen, but just in case :).
        if packet.body().is_empty() {
            return (FragmentProcessingState::NeedMoreFragments, None);
        }

        // Make sure body is a multiple of `FRAGMENT_BLOCK_SIZE` bytes, or
        // `packet` contains the last fragment block which is allowed to be less
        // than `FRAGMENT_BLOCK_SIZE` bytes.
        if m_flag && (packet.body().len() % (FRAGMENT_BLOCK_SIZE as usize) != 0) {
            return (FragmentProcessingState::InvalidFragment, None);
        }

        // Key used to find this connection's fragment cache data.
        let key = FragmentCacheKey::new(packet.src_ip(), packet.dst_ip(), id);

        // The number of fragment blocks `packet` contains.
        //
        // Note, we are calculating the ceiling of an integer division.
        // Essentially:
        //     ceil(packet.body.len() / FRAGMENT_BLOCK_SIZE)
        //
        // We need to calculate the ceiling of the division because the final
        // fragment block for a reassembled packet is allowed to contain less
        // than `FRAGMENT_BLOCK_SIZE` bytes.
        //
        // We know `packet.body().len() - 1` will never be less than 0 because
        // we already made sure that `packet`'s body is not empty, and it is
        // impossible to have a negative body size.
        let num_fragment_blocks = 1 + ((packet.body().len() - 1) / (FRAGMENT_BLOCK_SIZE as usize));
        assert!(num_fragment_blocks > 0);

        // The range of fragment blocks `packet` contains.
        //
        // The maximum number of fragment blocks a reassembled packet is allowed
        // to contain is `MAX_FRAGMENT_BLOCKS` so we make sure that the fragment
        // we received does not violate this.
        let fragment_blocks_range =
            if let Ok(offset_end) = u16::try_from((offset as usize) + num_fragment_blocks - 1) {
                if offset_end <= MAX_FRAGMENT_BLOCKS {
                    BlockRange { start: offset, end: offset_end }
                } else {
                    return (FragmentProcessingState::InvalidFragment, None);
                }
            } else {
                return (FragmentProcessingState::InvalidFragment, None);
            };

        // Get (or create) the fragment cache data.
        let (fragment_data, timer_not_yet_scheduled) = self.get_or_create(key);

        // Find the gap where `packet` belongs.
        let found_gap = match fragment_data.find_gap(fragment_blocks_range) {
            // We did not find a potential gap `packet` fits in so some of the
            // fragment blocks in `packet` overlaps with fragment blocks we
            // already received.
            None => {
                // Drop all reassembly data as per RFC 8200 section 4.5 (IPv6).
                // See RFC 5722 for more information.
                //
                // IPv4 (RFC 791) does not specify what to do for overlapped
                // fragments. RFC 1858 section 4.2 outlines a way to prevent an
                // overlapping fragment attack for IPv4, but this is primarily
                // for IP filtering since "no standard requires that an
                // overlap-safe reassemble algorithm be used" on hosts. In
                // practice, non-malicious nodes should not intentionally send
                // data for the same fragment block multiple times, so we will
                // do the same thing as IPv6 in this case.
                //
                // TODO(ghanan): Check to see if the fragment block's data is
                //               identical to already received data before
                //               dropping the reassembly data as packets may be
                //               duplicated in the network. Duplicate packets
                //               which are also fragmented are probably rare, so
                //               we should first determine if it is even
                //               worthwhile to do this check first. Note, we can
                //               choose to simply not do this check as RFC 8200
                //               section 4.5 mentions an implementation *may
                //               choose* to do this check. It does not say we
                //               MUST, so we would not be violating the RFC if
                //               we don't check for this case and just drop the
                //               packet.
                assert_matches!(self.remove_data(&key), Some(_));

                return (
                    FragmentProcessingState::InvalidFragment,
                    (!timer_not_yet_scheduled).then(|| CacheTimerAction::CancelExistingTimer(key)),
                );
            }
            Some(f) => f,
        };

        let timer_id = timer_not_yet_scheduled.then(|| CacheTimerAction::CreateNewTimer(key));

        // Remove `found_gap` since the gap as it exists will no longer be
        // valid.
        assert!(fragment_data.missing_blocks.remove(&found_gap));

        // If the received fragment blocks start after the beginning of
        // `found_gap`, create a new gap between the beginning of `found_gap`
        // and the first fragment block contained in `packet`.
        //
        // Example:
        //   `packet` w/ fragments [4, 7]
        //                 |-----|-----|-----|-----|
        //                    4     5     6     7
        //
        //   `found_gap` w/ fragments [X, 7] where 0 <= X < 4
        //     |-----| ... |-----|-----|-----|-----|
        //        X    ...    4     5     6     7
        //
        //   Here we can see that with a `found_gap` of [2, 7], `packet` covers
        //   [4, 7] but we are still missing [X, 3] so we create a new gap of
        //   [X, 3].
        if found_gap.start < fragment_blocks_range.start {
            assert!(fragment_data
                .missing_blocks
                .insert(BlockRange { start: found_gap.start, end: fragment_blocks_range.end - 1 }));
        }

        // If the received fragment blocks end before the end of `found_gap` and
        // we expect more fragments, create a new gap between the last fragment
        // block contained in `packet` and the end of `found_gap`.
        //
        // Example 1:
        //   `packet` w/ fragments [4, 7] & m_flag = true
        //     |-----|-----|-----|-----|
        //        4     5     6     7
        //
        //   `found_gap` w/ fragments [4, Y] where 7 < Y <= `MAX_FRAGMENT_BLOCKS`.
        //     |-----|-----|-----|-----| ... |-----|
        //        4     5     6     7    ...    Y
        //
        //   Here we can see that with a `found_gap` of [4, Y], `packet` covers
        //   [4, 7] but we still expect more fragment blocks after the blocks in
        //   `packet` (as noted by `m_flag`) so we are still missing [8, Y] so
        //   we create a new gap of [8, Y].
        //
        // Example 2:
        //   `packet` w/ fragments [4, 7] & m_flag = false
        //     |-----|-----|-----|-----|
        //        4     5     6     7
        //
        //   `found_gap` w/ fragments [4, Y] where MAX = `MAX_FRAGMENT_BLOCKS`.
        //     |-----|-----|-----|-----| ... |-----|
        //        4     5     6     7    ...   MAX
        //
        //   Here we can see that with a `found_gap` of [4, MAX], `packet`
        //   covers [4, 7] and we don't expect more fragment blocks after the
        //   blocks in `packet` (as noted by `m_flag`) so we don't create a new
        //   gap. Note, if we encounter a `packet` where `m_flag` is false,
        //   `found_gap`'s end value must be MAX because we should only ever not
        //   create a new gap where the end is MAX when we are processing a
        //   packet with the last fragment block.
        if found_gap.end > fragment_blocks_range.end && m_flag {
            assert!(fragment_data
                .missing_blocks
                .insert(BlockRange { start: fragment_blocks_range.end + 1, end: found_gap.end }));
        } else if found_gap.end > fragment_blocks_range.end && !m_flag && found_gap.end < u16::MAX {
            // There is another fragment after this one that is already present
            // in the cache. That means that this fragment can't be the last
            // one (must have `m_flag` set).
            return (FragmentProcessingState::InvalidFragment, timer_id);
        } else {
            // Make sure that if we are not adding a fragment after the packet,
            // it is because `packet` goes up to the `found_gap`'s end boundary,
            // or this is the last fragment. If it is the last fragment for a
            // packet, we make sure that `found_gap`'s end value is
            // `core::u16::MAX`.
            assert!(
                found_gap.end == fragment_blocks_range.end
                    || (!m_flag && found_gap.end == u16::MAX),
                "found_gap: {:?}, fragment_blocks_range: {:?} offset: {:?}, m_flag: {:?}",
                found_gap,
                fragment_blocks_range,
                offset,
                m_flag
            );
        }

        let mut added_bytes = 0;
        // Get header buffer from `packet` if its fragment offset equals to 0.
        if offset == 0 {
            assert_eq!(fragment_data.header, None);
            let header = get_header::<B, I>(&packet);
            added_bytes = header.len();
            fragment_data.header = Some(header);
        }

        // Add our `packet`'s body to the store of body fragments.
        let mut body = Vec::with_capacity(packet.body().len());
        body.extend_from_slice(packet.body());
        added_bytes += body.len();
        fragment_data.total_size += added_bytes;
        fragment_data.body_fragments.push(PacketBodyFragment::new(offset, body));

        // If we still have missing fragments, let the caller know that we are
        // still waiting on some fragments. Otherwise, we let them know we are
        // ready to reassemble and give them a key and the final packet length
        // so they can allocate a sufficient buffer and call
        // `reassemble_packet`.
        let result = if fragment_data.missing_blocks.is_empty() {
            FragmentProcessingState::Ready { key, packet_len: fragment_data.total_size }
        } else {
            FragmentProcessingState::NeedMoreFragments
        };

        self.increment_size(added_bytes);
        (result, timer_id)
    }

    /// Attempts to reassemble a packet.
    ///
    /// Attempts to reassemble a packet associated with a given
    /// `FragmentCacheKey`, `key`, and cancels the timer to reset reassembly
    /// data. The caller is expected to allocate a buffer of sufficient size
    /// (available from `process_fragment` when it returns a
    /// `FragmentProcessingState::Ready` value) and provide it to
    /// `reassemble_packet` as `buffer` where the packet will be reassembled
    /// into.
    ///
    /// # Panics
    ///
    /// Panics if the provided `buffer` does not have enough capacity for the
    /// reassembled packet. Also panics if a different `ctx` is passed to
    /// `reassemble_packet` from the one passed to `process_fragment` when
    /// processing a packet with a given `key` as `reassemble_packet` will fail
    /// to cancel the reassembly timer.
    fn reassemble_packet<B: ByteSliceMut, BV: BufferViewMut<B>>(
        &mut self,
        key: &FragmentCacheKey<I::Addr>,
        buffer: BV,
    ) -> Result<I::Packet<B>, FragmentReassemblyError> {
        let entry = match self.cache.entry(*key) {
            Entry::Occupied(entry) => entry,
            Entry::Vacant(_) => return Err(FragmentReassemblyError::InvalidKey),
        };

        // Make sure we are not missing fragments.
        if !entry.get().missing_blocks.is_empty() {
            return Err(FragmentReassemblyError::MissingFragments);
        }
        // Remove the entry from the cache now that we've validated that we will
        // be able to reassemble it.
        let (_key, data) = entry.remove_entry();
        self.size -= data.total_size;

        // If we are not missing fragments, we must have header data.
        assert_matches!(data.header, Some(_));

        // TODO(https://github.com/rust-lang/rust/issues/59278): Use
        // `BinaryHeap::into_iter_sorted`.
        let body_fragments = data.body_fragments.into_sorted_vec().into_iter().map(|x| x.data);
        I::Packet::reassemble_fragmented_packet(buffer, data.header.unwrap(), body_fragments)
            .map_err(|_| FragmentReassemblyError::PacketParsingError)
    }

    /// Gets or creates a new entry in the cache for a given `key`.
    ///
    /// Returns a tuple whose second component indicates whether a reassembly
    /// timer needs to be scheduled.
    fn get_or_create(&mut self, key: FragmentCacheKey<I::Addr>) -> (&mut FragmentCacheData, bool) {
        match self.cache.entry(key) {
            Entry::Occupied(e) => (e.into_mut(), false),
            Entry::Vacant(e) => {
                // We have no reassembly data yet so this fragment is the first
                // one associated with the given `key`. Create a new entry in
                // the hash table and let the caller know to schedule a timer to
                // reset the entry.
                (e.insert(FragmentCacheData::default()), true)
            }
        }
    }

    fn above_size_threshold(&self) -> bool {
        self.size >= self.threshold
    }

    fn increment_size(&mut self, sz: usize) {
        assert!(!self.above_size_threshold());
        self.size += sz;
    }

    fn remove_data(&mut self, key: &FragmentCacheKey<I::Addr>) -> Option<FragmentCacheData> {
        let data = self.cache.remove(key)?;
        self.size -= data.total_size;
        Some(data)
    }
}

/// Gets the header bytes for a packet.
fn get_header<B: ByteSlice, I: IpExt>(packet: &I::Packet<B>) -> Vec<u8> {
    match packet.as_ip_addr_ref() {
        IpAddr::V4(packet) => packet.copy_header_bytes_for_fragment(),
        IpAddr::V6(packet) => {
            // We are guaranteed not to panic here because we will only panic if
            // `packet` does not have a fragment extension header. We can only get
            // here if `packet` is a fragment packet, so we know that `packet` has a
            // fragment extension header.
            packet.copy_header_bytes_for_fragment()
        }
    }
}

/// A fragment of a packet's body.
#[derive(Debug, PartialEq, Eq)]
struct PacketBodyFragment {
    offset: u16,
    data: Vec<u8>,
}

impl PacketBodyFragment {
    /// Constructs a new `PacketBodyFragment` to be stored in a `BinaryHeap`.
    fn new(offset: u16, data: Vec<u8>) -> Self {
        PacketBodyFragment { offset, data }
    }
}

// The ordering of a `PacketBodyFragment` is only dependant on the fragment
// offset.
impl PartialOrd for PacketBodyFragment {
    fn partial_cmp(&self, other: &PacketBodyFragment) -> Option<Ordering> {
        self.offset.partial_cmp(&other.offset)
    }
}

impl Ord for PacketBodyFragment {
    fn cmp(&self, other: &Self) -> Ordering {
        self.offset.cmp(&other.offset)
    }
}

#[cfg(test)]
mod tests {
    use alloc::vec;

    use assert_matches::assert_matches;
    use ip_test_macro::ip_test;
    use net_types::{
        ip::{IpAddress, Ipv4, Ipv6},
        Witness,
    };
    use packet::{Buf, ParseBuffer, Serializer};
    use packet_formats::{
        ip::{IpProto, Ipv6ExtHdrType},
        ipv4::{Ipv4Packet, Ipv4PacketBuilder},
        ipv6::{Ipv6Packet, Ipv6PacketBuilder},
    };

    use super::*;
    use crate::{
        context::{
            testutil::{
                handle_timer_helper_with_sc_ref_mut, FakeCtx, FakeInstant, FakeSyncCtx,
                FakeTimerCtxExt,
            },
            InstantContext as _,
        },
        testutil::{assert_empty, FakeEventDispatcherConfig, FAKE_CONFIG_V4, FAKE_CONFIG_V6},
    };

    #[derive(Default)]
    struct FakeFragmentContext<I: Ip> {
        cache: IpPacketFragmentCache<I, FakeInstant>,
    }

    type FakeCtxImpl<I> =
        FakeCtx<FakeFragmentContext<I>, FragmentCacheKey<<I as Ip>::Addr>, (), (), (), ()>;
    type FakeSyncCtxImpl<I> = FakeSyncCtx<FakeFragmentContext<I>, (), ()>;

    impl<I: Ip> FragmentStateContext<I, FakeInstant> for FakeSyncCtxImpl<I> {
        fn with_state_mut<O, F: FnOnce(&mut IpPacketFragmentCache<I, FakeInstant>) -> O>(
            &mut self,
            cb: F,
        ) -> O {
            cb(&mut self.get_mut().cache)
        }
    }

    macro_rules! assert_frag_proc_state_ready {
        ($lhs:expr, $src_ip:expr, $dst_ip:expr, $fragment_id:expr, $packet_len:expr) => {{
            let lhs_val = $lhs;
            match lhs_val {
                FragmentProcessingState::Ready { key, packet_len } => {
                    if key == FragmentCacheKey::new($src_ip, $dst_ip, $fragment_id as u32)
                        && packet_len == $packet_len
                    {
                        (key, packet_len)
                    } else {
                        panic!("Invalid key or packet_len values");
                    }
                }
                _ => panic!("{:?} is not `Ready`", lhs_val),
            }
        }};
    }

    /// The result `process_ipv4_fragment` or `process_ipv6_fragment` should
    /// expect after processing a fragment.
    #[derive(PartialEq)]
    enum ExpectedResult {
        /// After processing a packet fragment, we should be ready to reassemble
        /// the packet.
        Ready { total_body_len: usize },

        /// After processing a packet fragment, we need more packet fragments
        /// before being ready to reassemble the packet.
        NeedMore,

        /// The packet fragment is invalid.
        Invalid,

        /// The Cache is full.
        OutOfMemory,
    }

    /// Get an IPv4 packet builder.
    fn get_ipv4_builder() -> Ipv4PacketBuilder {
        Ipv4PacketBuilder::new(
            FAKE_CONFIG_V4.remote_ip,
            FAKE_CONFIG_V4.local_ip,
            10,
            IpProto::Tcp.into(),
        )
    }

    /// Get an IPv6 packet builder.
    fn get_ipv6_builder() -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(
            FAKE_CONFIG_V6.remote_ip,
            FAKE_CONFIG_V6.local_ip,
            10,
            IpProto::Tcp.into(),
        )
    }

    /// Validate that IpPacketFragmentCache has correct size.
    fn validate_size<I: Ip>(cache: &IpPacketFragmentCache<I, FakeInstant>) {
        let mut sz: usize = 0;

        for v in cache.cache.values() {
            sz += v.total_size;
        }

        assert_eq!(sz, cache.size);
    }

    /// Processes an IP fragment depending on the `Ip` `process_ip_fragment` is
    /// specialized with.
    ///
    /// See [`process_ipv4_fragment`] and [`process_ipv6_fragment`] which will
    /// be called when `I` is `Ipv4` and `Ipv6`, respectively.
    fn process_ip_fragment<
        I: TestIpExt,
        SC: FragmentContext<I, C>,
        C: FragmentNonSyncContext<I>,
    >(
        sync_ctx: &mut SC,
        ctx: &mut C,
        fragment_id: u16,
        fragment_offset: u16,
        m_flag: bool,
        expected_result: ExpectedResult,
    ) {
        I::process_ip_fragment(sync_ctx, ctx, fragment_id, fragment_offset, m_flag, expected_result)
    }

    /// Generates and processes an IPv4 fragment packet.
    ///
    /// The generated packet will have body of size `FRAGMENT_BLOCK_SIZE` bytes.
    fn process_ipv4_fragment<SC: FragmentContext<Ipv4, C>, C: FragmentNonSyncContext<Ipv4>>(
        sync_ctx: &mut SC,
        ctx: &mut C,
        fragment_id: u16,
        fragment_offset: u16,
        m_flag: bool,
        expected_result: ExpectedResult,
    ) {
        let mut builder = get_ipv4_builder();
        builder.id(fragment_id);
        builder.fragment_offset(fragment_offset);
        builder.mf_flag(m_flag);
        let body =
            generate_body_fragment(fragment_id, fragment_offset, usize::from(FRAGMENT_BLOCK_SIZE));

        let mut buffer = Buf::new(body, ..).encapsulate(builder).serialize_vec_outer().unwrap();
        let packet = buffer.parse::<Ipv4Packet<_>>().unwrap();

        match expected_result {
            ExpectedResult::Ready { total_body_len } => {
                let _: (FragmentCacheKey<_>, usize) = assert_frag_proc_state_ready!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FAKE_CONFIG_V4.remote_ip.get(),
                    FAKE_CONFIG_V4.local_ip.get(),
                    fragment_id,
                    total_body_len + Ipv4::HEADER_LENGTH
                );
            }
            ExpectedResult::NeedMore => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::NeedMoreFragments
                );
            }
            ExpectedResult::Invalid => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::InvalidFragment
                );
            }
            ExpectedResult::OutOfMemory => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::OutOfMemory
                );
            }
        }
    }

    /// Generates and processes an IPv6 fragment packet.
    ///
    /// The generated packet will have body of size `FRAGMENT_BLOCK_SIZE` bytes.
    fn process_ipv6_fragment<SC: FragmentContext<Ipv6, C>, C: FragmentNonSyncContext<Ipv6>>(
        sync_ctx: &mut SC,
        ctx: &mut C,
        fragment_id: u16,
        fragment_offset: u16,
        m_flag: bool,
        expected_result: ExpectedResult,
    ) {
        let mut bytes = vec![0; 48];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        bytes[6] = Ipv6ExtHdrType::Fragment.into(); // Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(FAKE_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(FAKE_CONFIG_V6.local_ip.bytes());
        bytes[40] = IpProto::Tcp.into();
        bytes[42] = (fragment_offset >> 5) as u8;
        bytes[43] = ((fragment_offset & 0x1F) << 3) as u8 | if m_flag { 1 } else { 0 };
        bytes[44..48].copy_from_slice(&(fragment_id as u32).to_be_bytes());
        bytes.extend(
            generate_body_fragment(fragment_id, fragment_offset, usize::from(FRAGMENT_BLOCK_SIZE))
                .iter(),
        );
        let payload_len = (bytes.len() - Ipv6::HEADER_LENGTH) as u16;
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
        let mut buf = Buf::new(bytes, ..);
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();

        match expected_result {
            ExpectedResult::Ready { total_body_len } => {
                let _: (FragmentCacheKey<_>, usize) = assert_frag_proc_state_ready!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FAKE_CONFIG_V6.remote_ip.get(),
                    FAKE_CONFIG_V6.local_ip.get(),
                    fragment_id,
                    total_body_len + Ipv6::HEADER_LENGTH
                );
            }
            ExpectedResult::NeedMore => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::NeedMoreFragments
                );
            }
            ExpectedResult::Invalid => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::InvalidFragment
                );
            }
            ExpectedResult::OutOfMemory => {
                assert_matches!(
                    FragmentHandler::process_fragment::<&[u8]>(sync_ctx, ctx, packet),
                    FragmentProcessingState::OutOfMemory
                );
            }
        }
    }

    trait TestIpExt: crate::testutil::TestIpExt {
        const HEADER_LENGTH: usize;

        fn process_ip_fragment<SC: FragmentContext<Self, C>, C: FragmentNonSyncContext<Self>>(
            sync_ctx: &mut SC,
            ctx: &mut C,
            fragment_id: u16,
            fragment_offset: u16,
            m_flag: bool,
            expected_result: ExpectedResult,
        );
    }

    impl TestIpExt for Ipv4 {
        const HEADER_LENGTH: usize = packet_formats::ipv4::HDR_PREFIX_LEN;

        fn process_ip_fragment<SC: FragmentContext<Self, C>, C: FragmentNonSyncContext<Self>>(
            sync_ctx: &mut SC,
            ctx: &mut C,
            fragment_id: u16,
            fragment_offset: u16,
            m_flag: bool,
            expected_result: ExpectedResult,
        ) {
            process_ipv4_fragment(
                sync_ctx,
                ctx,
                fragment_id,
                fragment_offset,
                m_flag,
                expected_result,
            )
        }
    }
    impl TestIpExt for Ipv6 {
        const HEADER_LENGTH: usize = packet_formats::ipv6::IPV6_FIXED_HDR_LEN;

        fn process_ip_fragment<SC: FragmentContext<Self, C>, C: FragmentNonSyncContext<Self>>(
            sync_ctx: &mut SC,
            ctx: &mut C,
            fragment_id: u16,
            fragment_offset: u16,
            m_flag: bool,
            expected_result: ExpectedResult,
        ) {
            process_ipv6_fragment(
                sync_ctx,
                ctx,
                fragment_id,
                fragment_offset,
                m_flag,
                expected_result,
            )
        }
    }

    /// Tries to reassemble the packet with the given fragment ID.
    fn try_reassemble_ip_packet<
        I: TestIpExt,
        SC: FragmentContext<I, C>,
        C: FragmentNonSyncContext<I>,
    >(
        sync_ctx: &mut SC,
        ctx: &mut C,
        fragment_id: u16,
        total_body_len: usize,
    ) {
        let mut buffer: Vec<u8> = vec![0; total_body_len + I::HEADER_LENGTH];
        let mut buffer = &mut buffer[..];
        let key = FragmentCacheKey::new(
            I::FAKE_CONFIG.remote_ip.get(),
            I::FAKE_CONFIG.local_ip.get(),
            fragment_id.into(),
        );
        let packet = FragmentHandler::reassemble_packet(sync_ctx, ctx, &key, &mut buffer).unwrap();
        let expected_body = generate_body_fragment(fragment_id, 0, total_body_len);
        assert_eq!(packet.body(), &expected_body[..]);
    }

    /// Generates the body of a packet with the given fragment ID, offset, and
    /// length.
    ///
    /// Overlapping body bytes from different calls to `generate_body_fragment`
    /// are guaranteed to have the same values.
    fn generate_body_fragment(fragment_id: u16, fragment_offset: u16, len: usize) -> Vec<u8> {
        // The body contains increasing byte values which start at `fragment_id`
        // at byte 0. This ensures that different packets with different
        // fragment IDs contain bodies with different byte values.
        let start = usize::from(fragment_id)
            + usize::from(fragment_offset) * usize::from(FRAGMENT_BLOCK_SIZE);
        (start..start + len).map(|byte| byte as u8).collect()
    }

    /// Gets a `FragmentCacheKey` with the remote and local IP addresses hard
    /// coded to their test values.
    fn test_key<I: TestIpExt>(id: u32) -> FragmentCacheKey<I::Addr> {
        FragmentCacheKey::new(I::FAKE_CONFIG.remote_ip.get(), I::FAKE_CONFIG.local_ip.get(), id)
    }

    #[test]
    fn test_ipv4_reassembly_not_needed() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<Ipv4>::with_sync_ctx(FakeSyncCtxImpl::<Ipv4>::default());

        // Test that we don't attempt reassembly if the packet is not
        // fragmented.

        let builder = get_ipv4_builder();
        let body = [1, 2, 3, 4, 5];
        let mut buffer =
            Buf::new(body.to_vec(), ..).encapsulate(builder).serialize_vec_outer().unwrap();
        let packet = buffer.parse::<Ipv4Packet<_>>().unwrap();
        assert_matches::assert_matches!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FragmentProcessingState::NotNeeded(unfragmented) if unfragmented.body() == body
        );
    }

    #[test]
    #[should_panic(
        expected = "internal error: entered unreachable code: Should never call this function if the packet does not have a fragment header"
    )]
    fn test_ipv6_reassembly_not_needed() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<Ipv6>::with_sync_ctx(FakeSyncCtxImpl::<Ipv6>::default());

        // Test that we panic if we call `fragment_data` on a packet that has no
        // fragment data.

        let builder = get_ipv6_builder();
        let mut buffer =
            Buf::new(vec![1, 2, 3, 4, 5], ..).encapsulate(builder).serialize_vec_outer().unwrap();
        let packet = buffer.parse::<Ipv6Packet<_>>().unwrap();
        assert_matches::assert_matches!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FragmentProcessingState::InvalidFragment
        );
    }

    #[ip_test]
    fn test_ip_reassembly<I: Ip + TestIpExt>() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id = 5;

        // Test that we properly reassemble fragmented packets.

        // Process fragment #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            1,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            2,
            false,
            ExpectedResult::Ready { total_body_len: 24 },
        );

        try_reassemble_ip_packet(&mut sync_ctx, &mut non_sync_ctx, fragment_id, 24);
    }

    #[ip_test]
    fn test_ip_reassemble_with_missing_blocks<I: Ip + TestIpExt>() {
        let fake_config = I::FAKE_CONFIG;
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id = 5;

        // Test the error we get when we attempt to reassemble with missing
        // fragments.

        // Process fragment #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            1,
            true,
            ExpectedResult::NeedMore,
        );

        let mut buffer: Vec<u8> = vec![0; 1];
        let mut buffer = &mut buffer[..];
        let key = FragmentCacheKey::new(
            fake_config.remote_ip.get(),
            fake_config.local_ip.get(),
            fragment_id as u32,
        );
        assert_eq!(
            FragmentHandler::reassemble_packet(&mut sync_ctx, &mut non_sync_ctx, &key, &mut buffer)
                .unwrap_err(),
            FragmentReassemblyError::MissingFragments,
        );
    }

    #[ip_test]
    fn test_ip_reassemble_after_timer<I: Ip + TestIpExt>() {
        let fake_config = I::FAKE_CONFIG;
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id = 5;
        let key = test_key::<I>(fragment_id.into());

        // Make sure no timers in the dispatcher yet.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.get_ref().cache.size, 0);

        // Test that we properly reset fragment cache on timer.

        // Process fragment #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );
        // Make sure a timer got added.
        non_sync_ctx
            .timer_ctx()
            .assert_timers_installed([(key, FakeInstant::from(REASSEMBLY_TIMEOUT))]);
        validate_size(&sync_ctx.get_ref().cache);

        // Process fragment #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            1,
            true,
            ExpectedResult::NeedMore,
        );
        // Make sure no new timers got added or fired.
        non_sync_ctx
            .timer_ctx()
            .assert_timers_installed([(key, FakeInstant::from(REASSEMBLY_TIMEOUT))]);
        validate_size(&sync_ctx.get_ref().cache);

        // Process fragment #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            2,
            false,
            ExpectedResult::Ready { total_body_len: 24 },
        );
        // Make sure no new timers got added or fired.
        non_sync_ctx
            .timer_ctx()
            .assert_timers_installed([(key, FakeInstant::from(REASSEMBLY_TIMEOUT))]);
        validate_size(&sync_ctx.get_ref().cache);

        // Trigger the timer (simulate a timer for the fragmented packet)
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
            Some(key)
        );

        // Make sure no other times exist..
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.get_ref().cache.size, 0);

        // Attempt to reassemble the packet but get an error since the fragment
        // data would have been reset/cleared.
        let key = FragmentCacheKey::new(
            fake_config.local_ip.get(),
            fake_config.remote_ip.get(),
            fragment_id as u32,
        );
        let packet_len = 44;
        let mut buffer: Vec<u8> = vec![0; packet_len];
        let mut buffer = &mut buffer[..];
        assert_eq!(
            FragmentHandler::reassemble_packet(&mut sync_ctx, &mut non_sync_ctx, &key, &mut buffer)
                .unwrap_err(),
            FragmentReassemblyError::InvalidKey,
        );
    }

    #[ip_test]
    fn test_ip_fragment_cache_oom<I: Ip + TestIpExt>() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let mut fragment_id = 0;
        const THRESHOLD: usize = 8196usize;

        assert_eq!(sync_ctx.get_ref().cache.size, 0);
        sync_ctx.get_mut().cache.threshold = THRESHOLD;

        // Test that when cache size exceeds the threshold, process_fragment
        // returns OOM.

        while sync_ctx.get_ref().cache.size < THRESHOLD {
            process_ip_fragment(
                &mut sync_ctx,
                &mut non_sync_ctx,
                fragment_id,
                0,
                true,
                ExpectedResult::NeedMore,
            );
            validate_size(&sync_ctx.get_ref().cache);
            fragment_id += 1;
        }

        // Now that the cache is at or above the threshold, observe OOM.
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::OutOfMemory,
        );
        validate_size(&sync_ctx.get_ref().cache);

        // Trigger the timers, which will clear the cache.
        let timers = non_sync_ctx
            .trigger_timers_for(
                REASSEMBLY_TIMEOUT + Duration::from_secs(1),
                handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
            )
            .len();
        assert!(timers == 171 || timers == 293); // ipv4 || ipv6
        assert_eq!(sync_ctx.get_ref().cache.size, 0);
        validate_size(&sync_ctx.get_ref().cache);

        // Can process fragments again.
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );
    }

    #[ip_test]
    fn test_ip_overlapping_single_fragment<I: Ip + TestIpExt>() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id = 5;

        // Test that we error on overlapping/duplicate fragments.

        // Process fragment #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #0 (overlaps original fragment #0 completely)
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::Invalid,
        );
    }

    #[test]
    fn test_ipv4_fragment_not_multiple_of_offset_unit() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<Ipv4>::with_sync_ctx(FakeSyncCtxImpl::<Ipv4>::default());
        let fragment_id = 0;

        assert_eq!(sync_ctx.get_ref().cache.size, 0);
        // Test that fragment bodies must be a multiple of
        // `FRAGMENT_BLOCK_SIZE`, except for the last fragment.

        // Process fragment #0
        process_ipv4_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 (body size is not a multiple of
        // `FRAGMENT_BLOCK_SIZE` and more flag is `true`).
        let mut builder = get_ipv4_builder();
        builder.id(fragment_id);
        builder.fragment_offset(1);
        builder.mf_flag(true);
        // Body with 1 byte less than `FRAGMENT_BLOCK_SIZE` so it is not a
        // multiple of `FRAGMENT_BLOCK_SIZE`.
        let mut body: Vec<u8> = Vec::new();
        body.extend(FRAGMENT_BLOCK_SIZE..FRAGMENT_BLOCK_SIZE * 2 - 1);
        let mut buffer = Buf::new(body, ..).encapsulate(builder).serialize_vec_outer().unwrap();
        let packet = buffer.parse::<Ipv4Packet<_>>().unwrap();
        assert_matches!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FragmentProcessingState::InvalidFragment
        );

        // Process fragment #1 (body size is not a multiple of
        // `FRAGMENT_BLOCK_SIZE` but more flag is `false`). The last fragment is
        // allowed to not be a multiple of `FRAGMENT_BLOCK_SIZE`.
        let mut builder = get_ipv4_builder();
        builder.id(fragment_id);
        builder.fragment_offset(1);
        builder.mf_flag(false);
        // Body with 1 byte less than `FRAGMENT_BLOCK_SIZE` so it is not a
        // multiple of `FRAGMENT_BLOCK_SIZE`.
        let mut body: Vec<u8> = Vec::new();
        body.extend(FRAGMENT_BLOCK_SIZE..FRAGMENT_BLOCK_SIZE * 2 - 1);
        let mut buffer = Buf::new(body, ..).encapsulate(builder).serialize_vec_outer().unwrap();
        let packet = buffer.parse::<Ipv4Packet<_>>().unwrap();
        let (key, packet_len) = assert_frag_proc_state_ready!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FAKE_CONFIG_V4.remote_ip.get(),
            FAKE_CONFIG_V4.local_ip.get(),
            fragment_id,
            35
        );
        validate_size(&sync_ctx.get_ref().cache);
        let mut buffer: Vec<u8> = vec![0; packet_len];
        let mut buffer = &mut buffer[..];
        let packet =
            FragmentHandler::reassemble_packet(&mut sync_ctx, &mut non_sync_ctx, &key, &mut buffer)
                .unwrap();
        let mut expected_body: Vec<u8> = Vec::new();
        expected_body.extend(0..15);
        assert_eq!(packet.body(), &expected_body[..]);
        assert_eq!(sync_ctx.get_ref().cache.size, 0);
    }

    #[test]
    fn test_ipv6_fragment_not_multiple_of_offset_unit() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<Ipv6>::with_sync_ctx(FakeSyncCtxImpl::<Ipv6>::default());
        let fragment_id = 0;

        assert_eq!(sync_ctx.get_ref().cache.size, 0);
        // Test that fragment bodies must be a multiple of
        // `FRAGMENT_BLOCK_SIZE`, except for the last fragment.

        // Process fragment #0
        process_ipv6_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 (body size is not a multiple of
        // `FRAGMENT_BLOCK_SIZE` and more flag is `true`).
        let mut bytes = vec![0; 48];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        bytes[6] = Ipv6ExtHdrType::Fragment.into(); // Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(FAKE_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(FAKE_CONFIG_V6.local_ip.bytes());
        bytes[40] = IpProto::Tcp.into();
        bytes[42] = 0;
        bytes[43] = (1 << 3) | 1;
        bytes[44..48].copy_from_slice(&u32::try_from(fragment_id).unwrap().to_be_bytes());
        bytes.extend(FRAGMENT_BLOCK_SIZE..FRAGMENT_BLOCK_SIZE * 2 - 1);
        let payload_len = (bytes.len() - 40) as u16;
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
        let mut buf = Buf::new(bytes, ..);
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        assert_matches!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FragmentProcessingState::InvalidFragment
        );

        // Process fragment #1 (body size is not a multiple of
        // `FRAGMENT_BLOCK_SIZE` but more flag is `false`). The last fragment is
        // allowed to not be a multiple of `FRAGMENT_BLOCK_SIZE`.
        let mut bytes = vec![0; 48];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        bytes[6] = Ipv6ExtHdrType::Fragment.into(); // Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(FAKE_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(FAKE_CONFIG_V6.local_ip.bytes());
        bytes[40] = IpProto::Tcp.into();
        bytes[42] = 0;
        bytes[43] = 1 << 3;
        bytes[44..48].copy_from_slice(&u32::try_from(fragment_id).unwrap().to_be_bytes());
        bytes.extend(FRAGMENT_BLOCK_SIZE..FRAGMENT_BLOCK_SIZE * 2 - 1);
        let payload_len = (bytes.len() - 40) as u16;
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
        let mut buf = Buf::new(bytes, ..);
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let (key, packet_len) = assert_frag_proc_state_ready!(
            FragmentHandler::process_fragment::<&[u8]>(&mut sync_ctx, &mut non_sync_ctx, packet),
            FAKE_CONFIG_V6.remote_ip.get(),
            FAKE_CONFIG_V6.local_ip.get(),
            fragment_id,
            55
        );
        validate_size(&sync_ctx.get_ref().cache);
        let mut buffer: Vec<u8> = vec![0; packet_len];
        let mut buffer = &mut buffer[..];
        let packet =
            FragmentHandler::reassemble_packet(&mut sync_ctx, &mut non_sync_ctx, &key, &mut buffer)
                .unwrap();
        let mut expected_body: Vec<u8> = Vec::new();
        expected_body.extend(0..15);
        assert_eq!(packet.body(), &expected_body[..]);
        assert_eq!(sync_ctx.get_ref().cache.size, 0);
    }

    #[ip_test]
    fn test_ip_reassembly_with_multiple_intertwined_packets<I: Ip + TestIpExt>() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id_0 = 5;
        let fragment_id_1 = 10;

        // Test that we properly reassemble fragmented packets when they arrive
        // intertwined with other packets' fragments.

        // Process fragment #0 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #0 for packet #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            1,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            1,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #2 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            2,
            false,
            ExpectedResult::Ready { total_body_len: 24 },
        );

        try_reassemble_ip_packet(&mut sync_ctx, &mut non_sync_ctx, fragment_id_0, 24);

        // Process fragment #2 for packet #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            2,
            false,
            ExpectedResult::Ready { total_body_len: 24 },
        );

        try_reassemble_ip_packet(&mut sync_ctx, &mut non_sync_ctx, fragment_id_1, 24);
    }

    #[ip_test]
    fn test_ip_reassembly_timer_with_multiple_intertwined_packets<I: Ip + TestIpExt>() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());
        let fragment_id_0 = 5;
        let fragment_id_1 = 10;
        let fragment_id_2 = 15;

        // Test that we properly timer with multiple intertwined packets that
        // all arrive out of order. We expect packet 1 and 3 to succeed, and
        // packet 1 to fail due to the reassembly timer.
        //
        // The flow of events:
        //   T=0s:
        //     - Packet #0, Fragment #0 arrives (timer scheduled for T=60s).
        //     - Packet #1, Fragment #2 arrives (timer scheduled for T=60s).
        //     - Packet #2, Fragment #2 arrives (timer scheduled for T=60s).
        //   T=30s:
        //     - Packet #0, Fragment #2 arrives.
        //   T=40s:
        //     - Packet #2, Fragment #1 arrives.
        //     - Packet #0, Fragment #1 arrives (timer cancelled since all
        //       fragments arrived).
        //   T=50s:
        //     - Packet #1, Fragment #0 arrives.
        //     - Packet #2, Fragment #0 arrives (timer cancelled since all
        //       fragments arrived).
        //   T=60s:
        //     - Timeout for reassembly of Packet #1.
        //     - Packet #1, Fragment #1 arrives (final fragment but timer
        //       already triggered so fragment not complete).

        // Process fragment #0 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 for packet #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            2,
            false,
            ExpectedResult::NeedMore,
        );

        // Process fragment #2 for packet #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_2,
            2,
            false,
            ExpectedResult::NeedMore,
        );

        // Advance time by 30s (should be at 30s now).
        assert_empty(non_sync_ctx.trigger_timers_for(
            Duration::from_secs(30),
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Process fragment #2 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            2,
            false,
            ExpectedResult::NeedMore,
        );

        // Advance time by 10s (should be at 40s now).
        assert_empty(non_sync_ctx.trigger_timers_for(
            Duration::from_secs(10),
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Process fragment #1 for packet #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_2,
            1,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #1 for packet #0
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_0,
            1,
            true,
            ExpectedResult::Ready { total_body_len: 24 },
        );

        try_reassemble_ip_packet(&mut sync_ctx, &mut non_sync_ctx, fragment_id_0, 24);

        // Advance time by 10s (should be at 50s now).
        assert_empty(non_sync_ctx.trigger_timers_for(
            Duration::from_secs(10),
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        ));

        // Process fragment #0 for packet #1
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            0,
            true,
            ExpectedResult::NeedMore,
        );

        // Process fragment #0 for packet #2
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_2,
            0,
            true,
            ExpectedResult::Ready { total_body_len: 24 },
        );

        try_reassemble_ip_packet(&mut sync_ctx, &mut non_sync_ctx, fragment_id_2, 24);

        // Advance time by 10s (should be at 60s now)), triggering the timer for
        // the reassembly of packet #1
        non_sync_ctx.trigger_timers_for_and_expect(
            Duration::from_secs(10),
            [test_key::<I>(fragment_id_1.into())],
            handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
        );

        // Make sure no other times exist.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        // Process fragment #2 for packet #1 Should get a need more return value
        // since even though we technically received all the fragments, the last
        // fragment didn't arrive until after the reassembly timer.
        process_ip_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            fragment_id_1,
            2,
            true,
            ExpectedResult::NeedMore,
        );
    }

    #[test]
    fn test_no_more_fragments_in_middle_of_block() {
        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<Ipv4>::with_sync_ctx(FakeSyncCtxImpl::<Ipv4>::default());
        process_ipv4_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            0,
            100,
            false,
            ExpectedResult::NeedMore,
        );

        process_ipv4_fragment(
            &mut sync_ctx,
            &mut non_sync_ctx,
            0,
            50,
            false,
            ExpectedResult::Invalid,
        );
    }

    #[ip_test]
    fn test_cancel_timer_on_overlap<I: Ip + TestIpExt>() {
        const FRAGMENT_ID: u16 = 1;
        const FRAGMENT_OFFSET: u16 = 0;
        const M_FLAG: bool = true;

        let FakeCtxImpl { mut sync_ctx, mut non_sync_ctx } =
            FakeCtxImpl::<I>::with_sync_ctx(FakeSyncCtxImpl::<I>::default());

        let FakeEventDispatcherConfig {
            subnet: _,
            local_ip,
            local_mac: _,
            remote_ip,
            remote_mac: _,
        } = I::FAKE_CONFIG;
        let key = FragmentCacheKey::new(remote_ip.get(), local_ip.get(), FRAGMENT_ID.into());

        // Do this a couple times to make sure that new packets matching the
        // invalid packet's fragment cache key create a new entry.
        for _ in 0..=2 {
            process_ip_fragment(
                &mut sync_ctx,
                &mut non_sync_ctx,
                FRAGMENT_ID,
                FRAGMENT_OFFSET,
                M_FLAG,
                ExpectedResult::NeedMore,
            );
            assert_eq!(
                non_sync_ctx.timer_ctx().timers(),
                [(non_sync_ctx.now() + REASSEMBLY_TIMEOUT, key)]
            );

            process_ip_fragment(
                &mut sync_ctx,
                &mut non_sync_ctx,
                FRAGMENT_ID,
                FRAGMENT_OFFSET,
                M_FLAG,
                ExpectedResult::Invalid,
            );
            assert_eq!(non_sync_ctx.timer_ctx().timers(), [],);
        }
    }
}
