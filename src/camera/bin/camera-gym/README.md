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
