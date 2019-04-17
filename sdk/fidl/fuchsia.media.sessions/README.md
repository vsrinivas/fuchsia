# Media Session Service

Fuchsia's Media Session Service mediates access between media players which
choose to publish "media sessions" (from here on referred to as "sessions")
and other components of the system which want to observe or control those
sessions, such as a "Now Playing" UI, a remote control, or a system media
policy enforcer.

## Rendered Docs

* [FIDL](./service.fidl)
* [Dart]() // TODO(turnage): Add link when Fuchsia dart docs come up.
* [Rust](https://fuchsia-docs.firebaseapp.com/rust/fidl_fuchsia_media_sessions/index.html)

## Overview

The Media Session Service is a system FIDL service.

This document assumes familiarity with FIDL. If you are not familiar with
FIDL please see [the guide](docs/development/languages/fidl/README.md).

It begins with the `fuchsia.media.sessions.Session` protocol, which is a
player-agnostic protocol for controlling media sessions. Media players
implement this protocol, and through it they push information about their
status and capabilities to clients (such as metadata and playback rate).
They can also receive control commands such as `Play()` and `Pause()`
through the protocol.

The system service is a registrar for all the
`fuchsia.media.sessions.Session`s on the system. The service mediates
access to them for priviledged clients.

### Overview of Introducing Clients to Players

              |      Media Session Service    |
              |===============================|
Players ----> | Publisher                     |
              |                               |
              |                               |
              |                      Registry | <---- "Now Playing" UI, etc
              |===============================|

Players publish their `fuchsia.media.sessions.Session` implementation to
the service. In return the service provides them an id. Session ids are a
zircon handle for an event. An id's significance is only its kernel object
id, an unforgeable unique identifier the service associates with the
`fuchsia.media.sessions.Session`.

Clients of `fuchsia.media.sessions.Registry` can connect to a controller
using the `ConnectToSessionById()` method. The service mediates the
requests, but the client can pretend as if they have a direct connection to
the media player's `fuchsia.media.sessions.Session` protocol and their
expectations won't be violated.

Most of the time clients will care about whatever player is relevant to the
user instead of any one player in particular. The service indicates which
player is relevant to the user by labeling that player's session as
*active*. When a player pushes a playback status over their
`fuchsia.media.sessions.Session` in which it is "Playing", the session
becomes *active*. Only locally-originating sessions can compete for active
status on the device.

Clients of `fuchsia.media.sessions.Registry` will receive events when the
collection of registered sessions changes, an
`OnSessionsChange(SessionsChange change)` event with the delta.

Clients who connect to `fuchsia.media.sessions.Registry` will receive an
event any time a session becomes active (or on connection if a session is
already active): the `OnActiveSession(ActiveSession? session)` event. This
will give clients the id of the active session in case they want to connect
to that one.

At any given time, the *active* session is the locally originating session
which most recently broadcasted a "Playing" status and is still connected.

More sophisticated solutions to determining the relevant session exist at a
higher level than this service, but this hint is sufficient when a given
user's session is used for only one active media session at a time.

###  Life of a Status Event

In a scenario where Fictional Fuchsia Music Player has published a session
to the service, and both a "Now Playing" UI and a remote control are
connected to the session, this is what happens when the player sends a
playback status event:

1. The player sends a playback status event to the client of its
   `fuchsia.media.sessions.Session`.
2. The service receives the event.
3. The service sends the event to all the clients of that player, simulating a
   direct connection to the player.

The same thing in inverse occurs for playback controls.

## Usage

### From the Perspective of a Media Player

Media players must first implement `fuchsia.media.sessions.Session`. Then
they make their implementation available to interested parties on the
system through the service:

1. It hands a client end to its `fuchsia.media.sessions.Session`
   implementation to the service through its
   `fuchsia.media.sessions.Publisher` protocol via
   `PublishSession(Session controller)`.
2. The service assigns the session an id and holds on to the channel client
   end.

### From the Perspective of a Client

Clients, such as a "Now Playing" ui, connect to
`fuchsia.media.sessions.Registry`.  On connection and when any session
becomes active, they will receive the `OnActiveSession(ActiveSession?
active_session)` event, containing the id of the active session if one
exists.

1. It hands the server end of a channel for the
   `fuchsia.media.sessions.Session` protocol to the service with
   `ConnectToSessionById(id session_id, request<Session> controller_request)`.
2. If the service holds a session associated with the given id, it will
   hold on to the server end and simulate a direct connection to the media
   player for the client.
