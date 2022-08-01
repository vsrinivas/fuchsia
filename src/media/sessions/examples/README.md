# Introduction

Example player is a fake media player that publishes its media session with
`fuchsia.media.sessions2`.

Use `fx set` to create a core version of Fuchsia that includes the targets and
shards needed to run the example player with the mediasession cli tool:

    fx set core.x64 \
        --with "//src/media" \
        --args='core_realm_shards += [ "//src/media/sessions:mediasession_core_shard" ]';

Build Fuchsia and (re)start the target emulator with this configuration.
See [these configuration](https://fuchsia.dev/fuchsia-src/getting_started#configure-and-build-fuchsia) instructions for more details.

# Running

To create and start and example player, use `ffx component`. The
`mediasession_core_shard` added above installs a `mediasession-examples`
collection into the `/core` realm to run example player components under.

The basic workflow is:

1. `ffx component create /core/mediasession-examples:player fuchsia-pkg://fuchsia.com/example_player#meta/example_player.cm`
2. `ffx component start /core/mediasession-examples:player`

Note that `mediasession-players` is the collection name and *must* be a part of
the moniker of the created component. However, the name of the component,
`player` above, can be user-defined when invoking `ffx component create`.

It is possible to create and start multiple example players as long as they have
a different component name (e.g `player1`, `player2`).

You can see the created components with `ffx component list`:

    > fx ffx component list | grep mediasession-examples
    /core/mediasession-examples:player1
    /core/mediasession-examples:player2

Then run `fx shell mediasession_cli_tool` to control and monitor the example players'
state by session-id. For example:

    > fx shell mediasession_cli_tool ls
    [53106] State: None
    [48966] State: None
    [53106] State: Some(Playing)
    [48966] State: Some(Playing)
    [48966] State: Some(Paused)
