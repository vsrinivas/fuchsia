# Audio Mixer tests

These tests validate the core of Fuchsia's system audio mixing (our Mixer,
OutputFormatter and Gain objects) at a unit level, using tests in these areas:

1) **DataFormats**
2) **Pass-Thru**
3) **Gain**
4) **Timing**
5) **Frequency Response**
6) **Signal-to-Noise-and-Distortion (SINAD)**

Items 1 & 2 have been grouped into a __transparency tests__ file; item 3
(__gain tests__) includes overflow, underflow validation; item 4 is included
in the __resampler tests__. A set of test functions related to analyzing our
results are separated into their own __audio_analysis.cc__ source file and
tested in their own right. Items 5 & 6 use these test functions to perform
audio fidelity testing in the frequency domain, ensuring that our processing
does not color the input (frequency response) nor add additional artifacts
(signal-to-noise-and-distortion, or SINAD): __frequency tests__.


Future areas for mixer evaluation include:

7) **Dynamic Range**
8) **Impulse Response**
9) **Phase Response**


The frequency response and SINAD tests (as well as Noise Floor tests that
were previously considered transparency tests) have been added as normal
unit tests, as they are tightly related to these specific Mixer objects.
Fuller versions of frequency response, SINAD, dynamic range and phase tests
will be included in audio mixer profile tests that will execute post-submit,
rather than as a part of the CQ test set.

## Issues

Each Jira issue below represents a system behavior encountered during the
creation of these tests. Presumably, when/if each product issue is addressed,
the related test(s) will need some amount of rework as well; all of these
tests have been annotated, including the Jira item. That said, these tests
tightly focus on _current_ system behavior; as a rule they demonstrate how
the current system behaves _as-implemented_.

**Correctness - gain processing**

*   MTWN-71

    The Gain object clamps provided Output gains to a maximum of 24.0 dB,
treating Output and Renderer gains identically. This differs from documented
limits for Master (Output) gain: via the SetMasterGain API, this has a maximum
value of 0.0 dB.

*   MTWN-70

    The Gain object contains two functions, through which clients can provide
two (float) values and receive a (fixed-point) representation of their product.
The documented behavior of this object in multi-threaded scenarios should be
clarified (might be as easy as changing a "should" to a "must"). Depending on
whether single-threaded is a requirement, we will need additional product code
and tests.

**Correctness - interpolation**

*   MTWN-77

    On the last sample of a mix, for certain combinations of buffer sizes for
Dest and Source, the LinearSampler will point-sample instead of interpolate.

*   MTWN-78

    If by chance the source and destination buffers are both fully completed on
the last sample of a mix, the Mix function returns FALSE, which indicates that
the source buffer has not been fully consumed and should be held. Although
there could be scenarios in the future that take advantage of this, if the
definition of this return value is "whether the source buffer can be safely
released", then by this definition we should return TRUE in this case.

**Correctness - accumulation**

*   MTWN-82

    We mix each stream with the following sequence of steps: Normalize,
Channelize, Interpolate, Gain-Scale, Accumulate, Denormalize. After the
gain-scale step, we clamp each stream individually before accumulation. This in
large part eliminates the benefits of a wider accumulator, and is unnecessary
because the OutputFormatter object clamps the final mix before writing it to
the output buffer.

*   MTWN-83

    The accumulation step does not clamp values to the int32 range and can
overflow, given a sufficient number of incoming streams. This limit is
admittedly beyond any foreseeable scenario (65,000 streams), but this should be
documented even if the code does not explicitly clamp.

**Audio processing accuracy**

*   MTWN-73, MTWN-80

    Applying gain to audio values requires scale-up and scale-down operations
on both our fixed-point representation of gain and the resultant audio data,
while being mindful of integer container size and precision. In the process, we
down-scale the gain scalar (MTWN-73) and the resultant audio data (MTWN-80)
without first rounding, leading to truncation. Both of these contribute to our
producing results that (for certain inputs) are "expected-1". Again, these have
been accomodated in all related test code and annotated
appropriately.

*   MTWN-74

    During resampling, our interpolation uses *fractional* rates and positions
for the 'source' data as it creates each 'destination' sample on an *integer*
position. During interpolation, as we scale audio data up and down, we
down-scale without first rounding, leading to off-by-one inaccuracies.

*   MTWN-76

    Gain is applied to audio data during the interpolation-and-accumulation
process. As an optimization, if gain is lower than 160 dB for a given stream,
we skip any mixing and simply advance the positions accordingly. In the case
where the 'accumulate' flag is NOT set, we should also zero-out the destination
buffer. The proper fix for this might simply be to document this behavior,
since the OutputBase object does zero-out a mix buffer before providing it to
the Mixer object.

*   MTWN-81

    When mixing from stereo to mono, our calculations have a negative bias:
+1.5 rounds to 1, but -1.5 rounds to -2. This type of asymmetry causes slight
distortion. To accomodate this in today's code, certain expected numerical
results have been adjusted by 1 to reflect the reality of today's system.

*   MTWN-84

    When a final mix is "denormalized" from our internal representation into an
output buffer that has a format of unsigned-8-bit, we floor instead of round,
leading to results that for certain inputs are "expected-1". This also leads to
a slight negative DC bias for this output format.
