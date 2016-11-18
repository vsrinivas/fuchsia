# Video Player Example App

This directory contains an application that uses apps/media and apps/mozart to
create a video player.

## USAGE

The video player currently uses a file reader only, so you'll need to have an
accessible file. Here's an example command line:

  @ bootstrap launch video_player --path=/data/vid.ogv

In the future, we'll support a network reader as well.

The app responds to mouse clicks (touch on the Acer) and the keyboard. Mozart
requires a touch to focus the keyboard. Touching anywhere but the progress bar
toggles between play and pause. Touching the progress bar does a seek to that
point. The space bar toggles between play and pause. 'q' quits.
