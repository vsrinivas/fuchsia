# Peer-to-peer communication

The Ledger supports peer-to-peer synchronization between devices able to communicate directly.

# Protocol
The protocol used for P2P communication is based on message passing. Messages
are defined in //peridot/bin/ledger/p2p_sync/impl/message.fbs. This is not an
RPC-like system : most messages do not expect any response, and the protocol
should be resilient to devices not answering.
