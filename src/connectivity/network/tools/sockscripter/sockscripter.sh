
# source this script to get auto completion and adds `sockscripterh` and `sockscripterf` commands,
# to run sockscripter on host and on fuchsia (through fx shell), respectively.

function _sockscripter_path {
  local using=$(fx use 2> /dev/null | grep current | sed 's/ (current)//')
  echo "$FUCHSIA_DIR/$using/host_x64/sockscripter"
}

function _sockscripter {
  COMPREPLY=();
  local word="$2"
  local prev_word="$3"
  local bin_path=$(_sockscripter_path)
  if [ -z "$bin_path" ]; then
    return 1;
  fi
  if [ $COMP_CWORD -eq 1 ]; then
    if [ -z "$word" ]; then
      COMPREPLY=($(compgen -W "$($bin_path -s)"));
    else
      COMPREPLY=($(compgen -W "$($bin_path -s)" -- "$word"));
    fi
  else
    if [ $COMP_CWORD -eq 2 ]; then
      if ! $bin_path -p $prev_word; then
        COMPREPLY=($(compgen -W "$($bin_path -c)" -- "$word"));
      fi
    elif [[ -z "$word" ]]; then
      if ! $bin_path -a $prev_word; then
        COMPREPLY=($(compgen -W "$($bin_path -c)"));
      fi
    else
      if ! $bin_path -a $prev_word; then
        COMPREPLY=($(compgen -W "$($bin_path -c)" -- "$word"));
      fi
    fi
  fi
}

function sockscripterh() {
  $(_sockscripter_path) $@
}

function sockscripterf() {
  fx shell sockscripter $@
}

complete -F _sockscripter sockscripterh
complete -F _sockscripter sockscripterf
