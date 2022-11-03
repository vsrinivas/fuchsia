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
set -x FUCHSIA_DIR (readlink -f (dirname (dirname (status -f))))

# export PATH to include .jiri_root/bin
set jiri_root_bin "$FUCHSIA_DIR/.jiri_root/bin"
if not contains $jiri_root_bin $PATH
  set -x PATH $jiri_root_bin $PATH
end

# Add completion functions to $fish_complete_path
set fuchsia_completions "$FUCHSIA_DIR/scripts/fish/completions"
if not contains $fuchsia_completions $fish_complete_path
  set -x fish_complete_path $fuchsia_completions $fish_complete_path
end

set -x DEBUGINFOD_URLS (bash -c 'source "$FUCHSIA_DIR/tools/devshell/lib/vars.sh" && echo -n "$DEBUGINFOD_URLS"')
