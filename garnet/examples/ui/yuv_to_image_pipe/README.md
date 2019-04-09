# Example: YUV to Image Pipe

This directory contains an application which updates the scene using an
ImagePipe. By default, it presents a single image that moves around the screen
in a cyclic manner. When --input_driven is set, it swaps between images and
updates the content based on user clicks.

## Usage

```shell
$ present_view \
  fuchsia-pkg://fuchsia.com/yuv_to_image_pipe#meta/yuv_to_image_pipe.cmx --NV12
```

