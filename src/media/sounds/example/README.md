# Sound player example

This directory contains an example component that demonstrates the use of the
`fuchsia.media.sounds.Player` protocol with various audio content.

## Building

If these components are not present in your build, they can be added by
appending the following packages to your `fx set` command. For example:

```bash
$ fx set workstation.x64 --with //src/media/sounds/example \
    --with //src/media/sounds/soundplayer
$ fx build
```

## Running

Use `ffx session launch` to launch this component into a restricted realm
for development purposes:

```bash
$ ffx session launch fuchsia-pkg://fuchsia.com/soundplayer_example#meta/soundplayer_example.cm
```

The example component plays a set of built-in sounds by default, then exits.

### Playing custom sounds

You can override the default tones by placing custom sound files into `tmp` storage before the
component runs. To add custom sounds, do the following:

1.  Create the component instance:

    ```bash
    $ ffx session launch fuchsia-pkg://fuchsia.com/soundplayer_example#meta/soundplayer_example.cm
    ```

1.  Use `ffx session show` to discover the session component's "Instance ID":

    ```bash
    $ ffx session show
                Moniker: /core/session-manager/session:session
                    URL: fuchsia-pkg://fuchsia.com/soundplayer_example#meta/soundplayer_example.cm
                    Type: CML dynamic component
        Component State: Resolved
            Instance ID: 21cfa3a89262d5a856acab7166b36af59fcdaa2227256638cf0a6202e265a199
    ...
    ```

1.  Use `ffx component storage` to copy sound files from your workstation to the component's
    `tmp` storage directory:

    ```bash
    $ ffx component storage --capability tmp copy <local-sound-file> <instance-id>::/<sound-file>
    ```

1.  Start the component instance:

    ```bash
    $ ffx session restart
    ```
