# AudioCore Mixer Tools

## Performance Metrics

The AudioCore audio_mixer_profiler tool is intended to measure performance of different
stages in the audio mixing process.

## Running in CQ

The audio mixer profiler is run on CQ performance bots to test the impact of
individual CLs on the audio Performance Metrics. To run audio_mixer_profiler
performance tests in CQ, simply "Choose Tryjobs" and select
one of the perfcompare bots. This will run the audio performance
tests and output a comparison of performance before and after the CL, accessed
by clicking on the completed tryjob.

Performance results can be observed over time in [Chromeperf][chromeperf].

## Running Locally

To run the audio_mixer_profiler locally (without performance comparison provided
by perftest), ensure the tool is included in the build, and run
`fx shell audio_mixer_profiler`. This will run the performance tests across what
are considered "common" or "default" configurations. Flags can be used to configure
the profiler; find details using the `--help` flag.

To run the audio_mixer_profiler locally using perfcompare, refer to
[perfcompare][perfcompare].

## Interpreting Results

### Perfcompare

Perfcompare results are exported in a format that outlines each test case configuration.

For example:

`fuchsia.audio.mixer_creation: WindowedSinc/Float/Channels_1:1/FrameRates_008000:192000`
- `fuchsia.audio.mixer_creation` - suite to test mixer creation
- `WindowedSinc` - resampler type
- `Float` - sample format
- `Channels_1:1` - input channels : output channels
- `FrameRates_008000:192000` - source sample rate (Hz) : dest sample rate (Hz)

`fuchsia.audio.mixing: Point/Float/Channels_1:1/FrameRates_048000:048000/Mute-`
- `fuchsia.audio.mixing` - suite to test audio mixing
- `Point` - resampler type
- `Float` - sample format
- `Channels_1:1` - input channels : output channels
- `FrameRates_048000:048000` - source sample rate (Hz) : dest sample rate (Hz)
- `Mute` - gain factor
- `-` - accumulate yes (+) or no (-)

`fuchsia.audio.mixer_output: Float/Silence/Channels_1`
- `fuchsia.audio.mixer_output` - suite to test output production
- `Float` - sample format
- `Silence` - range of source data
- `Channels_1` - number of ouput channels

### Local (non-perfcompare)

In the profiler output, results are preceded by a legend describing the result format.


<!-- Reference links -->

[perfcompare]: /src/testing/perfcompare/README.md
[chromeperf]: /docs/development/performance/chromeperf_user_guide.md
