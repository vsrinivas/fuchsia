# `chunked-compression`

`chunked-compression` is a C++ library for compressing and decompressing data in
chunks, which enables random-access decompression of subsets of the file.

## Building

This library should be automatically included in builds.

## Using

Include the `//src/lib/chunked-compression` build target to make the library
available in your C++ application.

### Compression

Either streaming or one-shot compression is supported by the library, using the
`StreamingChunkedCompressor` and `ChunkedCompressor` objects respectively.

#### Streaming

Streaming compression requires the size of the input stream to be known ahead of
time but allows data to be compressed in chunks (which allows clients to avoid
buffering the entire input data before compressing).

```c++
// Assuming these methods are provided:

// Returns the size of the input stream
size_t InputDataSize();
// Reads up to |buf_len| bytes of the stream into |buf|, returning bytes read
size_t ReadInput(uint8_t* buf, size_t buf_len);
// Returns the max size of bytes to read at a time from the stream
size_t ReadBufferSize();

// The following code implements streaming compression:

size_t input_data_sz = InputDataSize();

StreamingChunkedCompressor compressor;
size_t output_limit = compressor.ComputeOutputSizeLimit(input_data_sz);

fbl::Array<uint8_t> output(new uint8_t[output_limit], output_limit);
compressor.Init(input_data_sz, output.get(), output.size());

uint8_t input[ReadBufferSize()];
size_t bytes_in;
while ((bytes_in = ReadInput(input, sizeof(input)))) {
  compressor.Update(input, bytes_in);
}

size_t compressed_size;
compressor.Final(&compressed_size);
```

#### One-Shot

For simplicity, a one-shot compression API also exists if the entire input can
be buffered.

```c++
const void* input = Input();
size_t input_len = InputLength();

ChunkedCompressor compressor;
size_t output_limit = compressor.ComputeOutputSizeLimit(input_len);

fbl::Array<uint8_t> output(new uint8_t[output_limit], output_limit);

size_t compressed_size;
compressor.Compress(input, input_len, output.get(), output.size(),
                    &compressed_size);
```

#### Tuning Compression

There are a number of tunable parameters available to control compression
behavior.

| Parameter       | Explanation                   | Range                      |
| --------------- | ----------------------------- | -------------------------- |
| Level           | Controls how aggressive       | 1-21 (Default = 3)         |
:                 : compression will              :                            :
:                 : be.<br><br>Higher values      :                            :
:                 : result in better compression  :                            :
:                 : ratios, but result in an      :                            :
:                 : exponential increase in       :                            :
:                 : compression time.             :                            :
| Chunk Size      | Controls the size of input    | 128k - 1M (Default = 128K) |
:                 : data frames.<br><br>Larger    :                            :
:                 : frames result in better       :                            :
:                 : compression ratios, but allow :                            :
:                 : for less granular access      :                            :
:                 : during decompression.         :                            :
| Header Checksum | Enables a checksum for the    | 0/1                        |
:                 : file header and seek table.   :                            :
| Frame Checksum  | Enables a per-frame checksum  | 0/1                        |
:                 : for each frame of data.       :                            :

### Decompression

Specific chunks of data can be decompressed independently of other chunks. A
seek table is stored in the head of the file which allows either decompressed or
compressed offsets to be looked up.

#### Single-Frame Decompression

```c++
// Assumes the following methods are implemented:

constexpr size_t kHeaderLength = 8192;
// Load the first kHeaderLength bytes into memory. (Header may be <8192 bytes,
// needs to be parsed to get size.)
const void* InputDataHeader();

// Returns the length of the compressed file.
size_t InputLength();

// Returns the byte offset of the decompressed data we wish to read.
size_t TargetOffset();

// Loads |size| bytes from the compressed file at |offset|.
const uint8_t* LoadCompressedData(size_t offset, size_t size);

// The following implements random-access decompression:

const void* header = InputDataHeader();

size_t compressed_length = InputLength(); // Assume >kHeaderLength

HeaderReader reader;
SeekTable table = reader.Parse(header, kHeaderLength, compressed_length);

ChunkedDecompressor decompressor;

size_t target_offset = TargetOffset();
unsigned table_index = table.EntryForDecompressedOffset(TargetOffset()).get();
const SeekTableEntry& entry = table.Entries()[table_index];
fbl::Array<uint8_t> output(new uint8_t[entry.decompressed_size],
                                  entry.decompressed_size);

size_t input_frame_size = entry.compressed_size;
const uint8_t *input_frame = LoadCompressedData(entry.compressed_offset, input_frame_size);

size_t bytes_written;
decompressor.DecompressFrame(table, table_index, input_frame, input_frame_size, output.get(),
                             output.size(), &bytes_written);
```

#### One-Shot Decompression

For simplicity, a one-shot decompression API also exists if the entire input can
be buffered.

```c++
const void* input = InputData();
size_t input_len = InputLength();

HeaderReader reader;
SeekTable table = reader.Parse(input, input_len, input_len);

size_t output_size = table.DecompressedSize();
fbl::Array<uint8_t> output(new uint8_t[output_size], output_size);

size_t bytes_witten;
ChunkedDecompresor decompressor;
decompressor.Decompress(table, input, input_len, output.get(), output.size(), &bytes_written);
```

## Testing

Include the `//src/lib/chunked-compression:tests` target in your build. (This
will automatically be included as part of `//src/lib:tests`).

To run the tests:

```
$ fx test chunked-compression-unittests
```
