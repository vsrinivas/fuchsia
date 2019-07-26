# Media Session Service

Fuchsia's Media Session Service mediates access between media players which
choose to publish "media sessions" (from here on referred to as "sessions")
and other components of the system which want to observe or control those
sessions, such as a "Now Playing" UI, a remote control, or a system media
policy enforcer.

## Rendered Docs

* [FIDL](https://fuchsia.dev/reference/fidl/fuchsia.media.sessions2)
* [Dart]() // TODO(turnage): Add link when Fuchsia dart docs come up.
* [Rust](https://fuchsia-docs.firebaseapp.com/rust/fidl_fuchsia_media_sessions2/index.html)

## Overview

A system service, the media session registry service (here on, Registry Service),
exposes two FIDL protocols: `Publisher` and `Discovery`.

Media players publish themselves over the `Publisher` protocol. Clients
interested in discovering ongoing media sessions on the system do so through
the `Discovery` protocol.

### Overview

              |        Registry Service       |
              |===============================|
Players ----> | Publisher                     |
              |                               |
              |                               |
              |                     Discovery | <---- "Now Playing" UI, etc
              |===============================|

### Example Use Case: A "Now Playing" UI

A client can watch for updates to all sessions using `Discovery.WatchSessions`.

```rust
let discovery_proxy = connect_to_service::<DiscoveryMarker>()?;
let (session_watcher, session_watcher_request) = create_endpoints()?;
let session_watcher_proxy = session_watcher.into_proxy()?;

let watcher = discovery_proxy.watch_sessions(
    WatchOptions::default(),
    session_watcher_request)?;

loop {
    let sessions_reply_future = session_watcher_proxy.watch_sessions()?;
    let sessions = await!(sessions_reply_future)?;
    for session in sessions {
        update_ui_for_session_with_id(
            session.id,
            session.player_status,
            session.media_images,
            session.metadata,
            ...);
    }
}
```

### Publishing a Player

Players must implement `Player` and publish themselves once with
`Publisher.Publish`.

```rust
let (player_client_end, player_server_end) = create_endpoints()?;
let publisher_proxy = connect_to_service::<PublisherMarker>()?;

spawn_implementation_to_serve(player_server_end);

publisher_proxy.publish(player_client_end, PlayerRegistration {
    domain: Some(PLAYER_DOMAIN),
});
```