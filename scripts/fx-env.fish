## usage: source scripts/fx-env.fish
##
## This file can be sourced within the shell or in your local
## fish config. To add it you can run the following:
##
##   mkdir -p ~/.config/fish/conf.d/
##   echo 'source ~/fuchsia/scripts/fx-env.fish' >> ~/.config/fish/conf.d/fuchsia.fish
##
## Replace ~/fuchsia with the location of your fuchsia checkout.

### NOTE!
###
### This is not a normal shell script that executes on its own.
###
### It's evaluated directly in a user's interactive shell using
### the `.` or `source` shell built-in.
###
### Hence, this code must be careful not to pollute the user's shell
### with variable or function symbols that users don't want.

# export $FUCHSIA_DIR to ../.. relative to this file location
set -x FUCHSIA_DIR (realpath -s (dirname (dirname (status -f))))

# export PATH to include .jiri_root/bin
set -x PATH $FUCHSIA_DIR/.jiri_root/bin $PATH

# Add completion functions to $fish_complete_path
set -x fish_complete_path $FUCHSIA_DIR/scripts/fish/completions $fish_complete_path
