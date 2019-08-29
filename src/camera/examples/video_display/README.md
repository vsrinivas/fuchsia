# Video Display Example

## Usage

To test against the virtual camera, make sure "camera_id" is initialized to "0"
in the source file simple_camera_view.cc, and then run the following command:

```shell
$ present_view fuchsia-pkg://fuchsia.com/video_display#meta/video_display.cmx
```


To test against the Pixel Book build-in USB camera, make sure "camera_id" is
initialized to "1" in the source file simple_camera_view.cc, and then run the
following command:

```shell
$ present_view fuchsia-pkg://fuchsia.com/video_display#meta/video_display.cmx
```
