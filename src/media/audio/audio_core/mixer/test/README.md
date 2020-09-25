# Audio Fidelity tests

These tests validate the core of Fuchsia's system audio mixing (our Mixer, OutputProducer and Gain
objects), using tests in these areas:

1) **Gain/Mute**
2) **Volume Ramping**
3) **Timing**
4) **Numerical Analysis**
5) **Noise Floor**
6) **Frequency Response**
7) **Signal-to-Noise-and-Distortion (SINAD)**
8) **Dynamic Range**
9) **Out-of-Band Rejection**

Items 1 & 2 are located in a file of __gain__ tests and include overflow, underflow validation; item
3 is related to interpolation precision and is included in __resampling__ tests. Item 4 is a set of
test functions related to how we analyze our results (such as the Fast Fourier Transform) that have
been separated into their own __analysis__ source file and tested in their own right. Items 5, 6 & 7
use these test functions to perform audio fidelity testing in the frequency domain, ensuring that
our processing does not color the input (frequency response) nor add additional artifacts (signal-
to-noise-and-distortion, or SINAD): __response__ tests. Item 8 (__range__ tests) measures the
accuracy of our gain control, as well as its impact on our noise floor. Item 9 is a variant on
item 7, specifically how well we reject incoming signals when sample-rate conversion should entirely
eliminate them.

Future areas for mixer evaluation may include:

10) **Phase Response**
11) **Impulse Response**


Cursory tests of Frequency Response, SINAD, Dynamic Range, Noise Floor and Out-of-Band Rejection
are part of the normal execution of this binary. Fuller versions are included in "full profile"
fidelity testing that can be executed from the command-line by adding the __--full__ flag.


## FrequencySet

The frequency-based tests (noise floor, frequency response, sinad, dynamic range and out-of-band
rejection) use a series of individual sinusoid waves, as inputs to the audio subsystem. Sinusoids
are universally used in this type of testing, because they are easily and repeatably generated, and
they cause predictable _responses_.

Note that although we use waves of various frequencies and amplitudes, we always send only a single
wave at a time. Future tests such as Intermodulation (SMPTE IM) or Difference Frequency Distortion
(DFD) may use multiple frequencies to target the effects that signals may have on each other.

The __summary__ versions of these tests use either a single frequency -- 1000 Hz -- or a short list
-- 40, 1000 and 12000 Hz. The __full__ versions use a list of 40 frequencies across the audible audio
spectrum, using the standard set of _3 frequencies per octave_ (20, 25, 31, 40, 50, 63, 80, 100, 125,
160, 200, ...) plus a few extra frequencies near 20-24 kHz. Eight additional out-of-band frequencies
are included, spanning from 25 kHz to 96 kHz, always taking frequency aliasing into account when
above the sample rate. These out-of-band frequencies are only used in Out-of-Band Rejection tests
(formerly measured as a part of SINAD tests).

Although sinusoids are continuous, we use them to generate _discrete_ signals: a series of snapshots
_sampled_ at specific instants in time. To characterize a waveform most effectively, it is best to
sample it at numerous places throughout its complete cycle (as opposed to just a few locations on
the waveform). This leads us to prefer test frequencies that are _not_ closely related to the core
sample rate frequency. For this reason, keeping our 48 kHz sample rate in mind, we choose 39 Hz
instead of 40 Hz, 997 Hz instead of 1000, and so on.

These reference frequencies are stored in the array kReferenceFreqs. Because the summary frequencies
will always be found in the reference frequencies, we store the sumary frequencies as an array of
the specific kReferenceFreqs indices that are also used in the summary tests.

A bool __UseFullFrequencySet__ specifies whether the full frequency range should be used. This is
set in main.cc, during test app startup, and referenced during the frequency tests as well as in the
recap section. This flag and the previously-mentioned frequency arrays (and constants for
array-length) are found in the static class __FrequencySet__.


## AudioResult

For each of the frequency tests, the results are saved in various members of the static class
__AudioResult__. For multi-frequency tests, these are stored in arrays of length
_kNumInBandReferenceFreqs_ (if only audible frequencies are measured) or _kNumReferenceFreqs_ (if
out-of-band frequencies are included, e.g. when measuring aliasing). Results are stored in
double-precision float format, and are precisely compared to previous results, which are also stored
in constexpr arrays within AudioResult. In the absence of code change, the measurements should be
exactly the same each time, so the measured results are compared strictly to the previous results,
with any regression causing a failure. The expectation is that any code change that causes a
regression in these metrics would likely be coming from the media team, and if the code is
sufficiently important to cause a regression in our audio quality, then the CL would carry with it
an appropriate change to the AudioResult thresholds.

The terminology used in the audio tests is quite specific and intentional. A _level_ is the
magnitude (in decibels) of the response, when a test signal is provided. The term _noise_ often
refers to the magnitude of all other frequencies (in dB RMS, hence combined via root-sum-square)
besides the intended frequency. For some people, _noise_ excludes frequencies that are harmonics
(multiples) of the signal frequency, calling these _distortion_. A _sinad_ measurement, then, is a
more accurate term for exactly this: the ratio of _signal_ to _noise and distortion_.

The limits that are stored in AudioResult are all either _minimum_ values or _tolerances_. The
minimum values include frequency response and sinad; all test code referencing these values should
EXPECT_GE. The tolerances (always explicitly called by this term) are always compared in symmetric
manner, on both sides of the expected level.

### Updating AudioResult thresholds

Frequency response or SINAD failures include the measured value in the log, at the point that the
failure is surfaced. If the intention is to update AudioResult in a way that essentially accepts the
new result as the expected value, then that value (at eight total digits of precision) can be used.
For more significant updates to AudioResult values, the __--dump__ flag is available. This option
automatically includes all frequencies (i.e. it implies __--full__); following the run, all measured
values are displayed in a format that is easily copied into audio_result.cc. Note that these values
will be displayed with 9 digits of precision, so care must be taken when including them in
audio_result.cc. The rule of thumb is to use only eight total digits of precision, and to err on the
side of "more loose" when reducing the number of digits. Generally this means that for tolerance
thresholds and frequency response, any additional digit should be "ceiling-ed" up (a frequency
response measurement of -1.57207701 should be saved as -1.5720771); however, for noise floor,
out-of-band rejection and dynamic range, the additional digit would be "floored" away (a SINAD of
19.3948736 would be saved as the slightly-less-strict 19.394873, while a SINAD measurement of
-19.3948736 would be saved as -19.394874, also slightly less tight).


## Performance Profiling

The audio_fidelity_tests test binary also contains the ability to profile the performance of the Mixer,
Gain and OutputProducer classes. Use the __--profile__ flag to trigger these micro-benchmark tests,
which use *zx::clock::get_monotonic()* to measure the time required for a target to execute Mix() or
ProduceOutput() calls (for Mixer/Gain or OutputProducer objects, respectively) to generate 64k
frames. The aggregated results that are displayed for each permutation of parameters represent the
time consumed *per-call*, although to determine a relatively reliable Mean we run these
micro-benchmarks many tens or even hundreds of times. As is often the case with performance
profiling, one should be mindful not to directly compare results from different machines; generally
this profiling functionality should be used to provide a general sense of "before versus after" with
regards to a specific change that affects the mixer pipeline.


## Issues

Each Jira issue below represents a system behavior encountered during the creation of these tests.
Presumably, when/if each product issue is addressed, the related test(s) will need some amount of
rework as well; all of these tests have been annotated, including the Jira item. That said, these
tests tightly focus on _current_ system behavior; as a rule they demonstrate how the current system
behaves _as-implemented_.

Below, the existing mixer-related bugs are classified in groups related to their stage in the flow
of audio through the mixer:

*   fxbug.dev/13373

    Today, interpolation and media scheduling is performed using audio sample positions that are
represented by a 32-bit integer, in fixed-point form: 19 bits of integer, and 13 bits of fraction.
This by design puts a limit on the accuracy of our interpolating sample-rate converters. By
increasing the number of fractional bits, we can improve our SRC quality.

*   fxbug.dev/13361

    When enabling NxN channel passthru in our resamplers, there was significant code duplication.
This could be refactored to increase code reuse, leading to higher code resilience and easier future
extensibility.

*   fxbug.dev/13332

    In addition to the existing resamplers (SampleAndHold, LinearInterpolation), we should create
new ones with increased fidelity. This would more fully allow clients to make the
quality-vs.-performance tradeoff themselves.

*   fxbug.dev/13356

    The Gain object contains two functions, through which clients can provide two (float) values and
receive a (fixed-point) representation of their product. The documented behavior of this object in
multi-threaded scenarios should be clarified (might be as easy as changing a "should" to a "must").
Depending on whether single-threaded is a requirement, we will need additional product code and
tests.

*   fxbug.dev/13374

    The AudioRenderer API schedules each incoming audio packet on an integer sample boundary,
regardless of whether a fractional sample location would better represent the timestamp specified.
This bug represents the investigation (and potential enabling) of scheduling these packets on
fractional sample positions.

*   fxbug.dev/13379

    The AudioSampleFormat enum includes entries for ALL and NONE, with the intention that these
would be used in future scenarios. Until then, however, it might clarify things for client
developers if we remove these.
