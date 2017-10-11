# Simple Sine Sync Example App

This example is similar to the [Simple Sine example](../simple_sine/README.md)
but uses synchronous interfaces to show how to perform audio playback without
relying on a particular message loop interface. It plays the same audio as
the simple sine example and has the same setup scheme, but controls playback
in a slightly different way.

## Playback

### Warm-up

The example first computes a presentation time for the first packet based on an
estimate of the warm-up time of the system and populates a few frames of data.

### Playback

Once a few packets are prepared, the example initiates playback (by configuring
the timeline transform) and begins its primary playback loop. The playback
loop's job is to maintain a minimum buffer of pending packets in flight until
playback is complete. This loop is composed of 3 steps:

1.) Look at the current time and compute how many packets need to be supplied in
order to restore the desired buffer

2.) Generate and supply this many packets

3.) Sleep until it is time to generate more packets
