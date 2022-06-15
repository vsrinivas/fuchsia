# VAAPI Codecs

The goal of the VAAPI codecs is to provide hardware accelerated encoding and
decoding of video media on platforms that support libva.

## Building

### Build from source

To build the VAAPI codec adapters from source and not include the prebuilt
packages, you must set `use_prebuilt_codec_runner_intel_gen=false` in the build
arguments. You must also download the download dependencies like libva and
media-driver. To accomplish this run `jiri init -fetch-optional=vaapi-intel &&
jiri update`. For more information see the README located in the
[media-driver repo](https://fuchsia.googlesource.com/third_party/github.com/intel/media-driver/+/main/README.fuchsia.md).

### Using prebuilt binary

By default the workstation configuration will include the prebuilt binary from
CIPD. To force the issue you can set `use_prebuilt_codec_runner_intel_gen=true`
in the build arguments but this not necessary as it is the default behavior.

## Uploading

Any changes to the the
[//src/media/codec/codecs/vaapi](/src/media/codec/codecs/vaapi) folder and
dependencies will not be automatically be included in the output image. In order
to satisfy Fuchsia ABI requirements, the `codec_runner_intel_gen package` must
be included as a prebuilt meaning that any changes must be compiled locally and
uploaded to CIPD to be included in the output image. To upload the built
`codec_runner_intel_gen` package to CIPD follow the instructions in the README
located in the
[media-driver repo](https://fuchsia.googlesource.com/third_party/github.com/intel/media-driver/+/main/README.fuchsia.md#uploading-to-cipd-and-rolling).

## Usage

### Linear Mode

By default the codec will provide the outputted picture format in a linear NV12
format. Linear format is the easiest to understand since each offset in the
picture can be easily found by simple equation `offset = (row * pitch) + col`.
However there are many reasons this is not the most optimal way to provide
picture data:

1.  Since the underlying hardware does not support this memory format for its
    decode picture buffers (DPB), a deswizzling operation must take place in
    order to convert from the DPB memory format to the linear format. This
    process runs on the processor takes up valuable clock cycles, decreasing
    throughput.
2.  Since the DPB can't be in a linear format, the codec will copy from the DPB
    to the VMO while it is deswizzling. This further decreases performance and
    increases the memory usage.
3.  While in a linear format any algorithms that access adjacent rows will
    causes more cache line misses. Since the pitch generally exceeds the cache
    line size on processors, accessing adjacent rows will cause cache line
    missing and decrease the throughput of the algorithm. In the tiled format,
    adjacent rows are kept closer in memory greatly increasing the chance that
    the two items are in the same cache line.

If at all possible avoid the linear format in favor of the tiled format. If you
do need to use the linear format you can either set the `has_format_modifier` to
`false` or set `has_format_modifier` to `true` and `format_modifier` to
`FORMAT_MODIFIER_LINEAR` like so ...

```cpp
fuchsia::sysmem::BufferCollectionConstraints constraints;
constraints.image_format_constraints_count = 1;
auto& image_format = constraints.image_format_constraints[0];
image_format.color_spaces_count = 1;
image_format.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
image_format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
image_format.pixel_format.has_format_modifier = true; // Can set this to false as well
image_format.pixel_format.format_modifier.value =
    fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
```

### Tiled Format

As show above there are many benefits to using a tiled format. When using the
VAAPI codec, it is highly recommended to request a tiled format for the
performance benefit. In order to request a tiled format from the codec, set the
`has_format_modifier` to `true` and `format_modifier` to
`FORMAT_MODIFIER_INTEL_I915_Y_TILED` like so ...

```cpp
fuchsia::sysmem::BufferCollectionConstraints constraints;
constraints.image_format_constraints_count = 1;
auto& image_format = constraints.image_format_constraints[0];
image_format.color_spaces_count = 1;
image_format.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
image_format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
image_format.pixel_format.has_format_modifier = true;
image_format.pixel_format.format_modifier.value =
    fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED;
```

The returned
[CodecPackets](/src/media/lib/codec_impl/include/lib/media/codec_impl/codec_packet.h)
buffers from the codec will be the DPB. Since the codec is directly sharing the
output from the DPB, the client should not modify any data since it can be used
as a reference frame for intercoding algorithms.
