#!/bin/bash
#
# Usage:
# $ hermetic_golden_test_update_goldens.sh <test_package_name> <testdata_directory>
#
# The program $AUDIO_DIFF_PROGRAM will be used to show multiple audio files for
# inspection. If this is not specified, it defaults to "audacity" on linux and
# "open" on MacOS.

PKG=$1
TESTDATA=`realpath $2`

if [[ -z "$PKG" || -z "$TESTDATA" ]]; then
  echo "Usage: $0 <test_package_name> <testdata_directory>"
  exit 1
fi
if [ ! -d "$TESTDATA" ]; then
  echo "$TESTDATA not found; must be a directory containing expected test outputs"
  echo "Usage: $0 <test_package_name> <testdata_directory>"
  exit 1
fi

if [[ -z "$AUDIO_DIFF_PROGRAM" ]]; then
  case `uname -s` in
    Linux*)
      AUDIO_DIFF_PROGRAM=audacity
      ;;
    Darwin*)
      AUDIO_DIFF_PROGRAM=open
      ;;
    *)
      echo "Unknown OS: `uname -s`"
      exit
      ;;
  esac
fi

HOST_DIR=/tmp/audio-hermetic-golden-tests/$PKG/`date +%Y%m%d-%I%M%S`
REALM=audio-hermetic-golden-tests-$PKG
DEVICE_DIR=/data/cache/r/sys/r/$REALM/fuchsia.com:$PKG:0#meta:$PKG-component.cmx/

echo "Running fx test $PKG"
fx test "--realm=$REALM" $PKG -- --save-inputs-and-outputs

echo "Copying outputs to $HOST_DIR"
mkdir -p $HOST_DIR
ln -s -f $HOST_DIR /tmp/audio-hermetic-golden-tests/$PKG/latest
cd $HOST_DIR
for f in `fx shell ls $DEVICE_DIR`; do
  fx cp --to-host $DEVICE_DIR/$f $f
done

echo $PWD
for input in `ls *_input.wav`; do
  ringbuffer=`echo $input | sed -E 's/(.*)_input.wav/\1_ring_buffer.wav/'`
  newoutput=`echo $input | sed -E 's/(.*)_input.wav/\1_output.wav/'`
  oldoutput="$TESTDATA/"`echo $input | sed -E 's/(.*)_input.wav/\1_expected_output.wav/'`
  echo

  # If the test case does not have an _output.wav, it doesn't use goldens.
  if [ ! -f $newoutput ]; then
    echo "Skipping $input, test does not use a golden output"
    continue
  fi

  # Update this golden.
  echo "=========================================="
  echo "Input file: $input"
  echo "Ring buffer file: $ringbuffer"
  echo "New output file: $newoutput"
  echo "Old output file: $outoutput"
  if [ -f $oldoutput ]; then
    # Compare to an existing output.
    $AUDIO_DIFF_PROGRAM $HOST_DIR/{$input,$ringbuffer,$newoutput} $oldoutput
  else
    # Test being run for the first time.
    echo "Existing output file not found; assuming the test is being run for the first time"
    $AUDIO_DIFF_PROGRAM $HOST_DIR/{$input,$ringbuffer,$newoutput}
  fi
  read -p "Accept the new output? " yn
  case $yn in
    [Yy]*)
      echo "Overwriting $oldoutput"
      cp -f $newoutput $oldoutput
      ;;
  esac
done
