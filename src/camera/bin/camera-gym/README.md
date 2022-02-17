# Fuchsia Camera Gym (camera3 config exerciser)
Example app to exercise the various stream configurations available for Sherlock.

## How To Select Config

TBD

## How To Run (from fx shell while a Session is running)
This method will not allow passing of command line arguments.
All that can be done is to start running camera-gym.

sessionctl add_mod fuchsia-pkg://fuchsia.com/camera-gym#meta/camera-gym.cmx

## How To Run (from fx shell without a Session running)
This method will allow command line arguments.
Arguments can be specified as shown below.

### How To Shutdown Session

fx shell basemgr_launcher shutdown

### How to Run (using present_view)

fx shell present_view fuchsia-pkg://fuchsia.com/camera-gym#meta/camera-gym.cmx [ args ... ]

### How to Run (using tiles_ctl)

fx shell tiles_ctl start
fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/camera-gym#meta/camera-gym.cmx [ args ... ]
fx shell tiles_ctl stop

### How to Restart Session

fx shell run basemgr.cmx

### How to Run Manual Mode

camera-gym-ctl accepts lists of commands and sends them, one at a time, to camera-gym.
camera-gym must be started in manual mode. (Use "fx camera-gym --manual".)

There are short scripts that run these configs + streams in cycles:

### How to Run Manual Mode Without Session

camera-gym can be started in manual mode without a session running:

fx shell present_view fuchsia-pkg://fuchsia.com/camera-gym#meta/camera-gym-manual.cmx

### How to Run Manual Mode Scripts

test_simple.sh

  Running this bash script will cycle through all configs and all streams forever.

test_crop.sh

  Running this bash script will cycle through crop settings in steps of 1/40's.

### How to turn text descriptions on/off

#### Turn off descriptions
fx shell camera-gym-ctl --set-description=0

#### Turn on descriptions
fx shell camera-gym-ctl --set-description=1

### How to Capture a Frame in Manual Mode

Caveat (b/200839146): The current frame capture feature only works while a single stream is running.
Trying to do so with multiple streams running at the same time will result capturing from a stream
randomly selected from all of those running.

#### Capture a frame
fx shell camera-gym-ctl --capture-frame=0

#### Review how many frames were captured
fx shell ls /data/r/sys/r/session-0/fuchsia.com:camera-gym:0#meta:camera-gym-manual.cmx/.

#### Copy out frames that were captured
mkdir -p /tmp/my_dest_dir
fx scp "[$(fx get-device-addr)]:"/data/r/sys/r/session-0/fuchsia.com:camera-gym:0#meta:camera-gym-manual.cmx/image_\* /tmp/my_dest_dir/.

#### Post process NV12 raw dumps to PNG and view the PNG images
cd /tmp/my_dest_dir
ls -1 image*.nv12 | sed -e 's@\(.*_\)\([0-9][0-9]*x[0-9][0-9]*\)\(.*\).nv12@ffmpeg -f rawvideo -pixel_format nv12 -video_size \2 -i \1\2\3.nv12 \1\2\3.png@' > CONVERT.SH
. CONVERT.SH

#### Visually review converted PNG images (if on same system)

eog *.png

#### Visually review converted PNG images (if on another system)

rsync -av remote.system.somewhere.com:/tmp/my_dest_dir /tmp/.
cd /tmp/my_dest_dir
eog *.png
