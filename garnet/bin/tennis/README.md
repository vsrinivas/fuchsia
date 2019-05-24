# FIDL Tennis

Out of our undying love for the sport of tennis, we have created a
high-fidelity, realistic tennis simulation game, complete with
physically-simulated ball bounce physics and racket movement. Unfortunately, our
3d modeler is on vacation, so we currently render through ASCII art:

    example_ai                                                        example_ai
    2                                                                          3
                                          |
                                          |
                                          |
                            0             |
    )                                     |
                                          |                                    (
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |
                                          |

You can build an AI in any FIDL-supported language and have them fight each
other, or use the `manual_player` script to play against an AI by yourself.

## Running the Game

Set your packages to include Tennis:

```
fx set core.x64 --with //garnet/bin/tennis
fx build
```

After either paving or starting QEMU, you'll want to open three `fx shell`s. In
the first, you'll want to `run tennis_viewer`, and in the other two `run
tennis_example_ai`.

## Writing Your Own AI

We've written an example AI in Rust in the `garnet/bin/tennis/bots/example_ai`
folder. It's not very good, so you should be able to beat it! To make your own
AI using the base as an example, you can copy the `example_ai` folder to your
own, taking care to rename `meta/tennis_example_ai.cmx` and the various rules in
your bot's `BUILD.gn` file to create a new package. You'll also want to add your
bot to the list at `garnet/packages/experimental/disabled/tennis` and rerun `fx set` so
it will build.

The example is in Rust, but you can definitely build an AI in any FIDL-supported
language! The FIDL service definition is available at
`sdk/fidl/fuchsia.game.tennis/tennis.fidl`.
