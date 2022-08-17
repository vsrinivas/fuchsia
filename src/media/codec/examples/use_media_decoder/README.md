# use_media_decoder

`use_media_decoder` is manual tool for interacting with some of the system's
available codecs.

## Building

Make sure that `//src/media/codec/examples:use_media_decoder` is part of your
`fx set`. If it wasn't previously, restart your emulator or OTA the build to
your target after adding it and building.

## Using

`use_media_decoder` can be used from the shell. To provide input data files for
the command, first determine your target's IP address.

```
$ fx ffx target list
NAME                      SERIAL       TYPE                   STATE      ADDRS/IP                           RCS
fuchsia-54b2-0389-6769    <unknown>    workstation_eng.x64    Product    [fe80::d919:d797:efed:d078%br0,    Y
                                                                          192.168.42.210]
```

Copy the file to the target using IPv4...
```
$ fx scp myfile.vp9 fuchsia@192.168.42.210:/tmp/myfile.vp9
test-25fps.vp9        100%   86KB  22.8MB/s   00:00
```

...or IPv6
```
$ fx scp -6 myfile.vp9 fuchsia@\[fe80::d919:d797:efed:d078%br0\]:/tmp/myfile.vp9
test-25fps.vp9        100%   86KB  22.8MB/s   00:00
```

Now run `use_media_decoder`.
```
$ fx shell use_media_decoder --vp9 /tmp/myfile.vp9
WaitForSysmemBuffersAllocated() done - is_output: 0 buffer_count: 2
VP9 input stream: 1 stream_frame_ordinal: 0 input_pts_counter: 0 frame_header.size_bytes: 0x29b2
WaitForSysmemBuffersAllocated() done - is_output: 1 buffer_count: 10
VP9 input stream: 1 stream_frame_ordinal: 1 input_pts_counter: 1 frame_header.size_bytes: 0x9c8
VP9 input stream: 1 stream_frame_ordinal: 2 input_pts_counter: 2 frame_header.size_bytes: 0x7b
VP9 input stream: 1 stream_frame_ordinal: 3 input_pts_counter: 3 frame_header.size_bytes: 0x6f
...
```

You can use `fx scp` in the other direction to copy off any output files your
command generates.
