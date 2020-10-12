#!/usr/bin/env bash

set -uo pipefail

# source this script to get auto completion and adds `sockscripterh` and `sockscripterf` commands,
# to run sockscripter on host and on fuchsia (through fx shell), respectively.

function _sockscripter() {
  local bin_path
  if ! bin_path=$(fx get-build-dir)/$(fx list-build-artifacts --expect-one --name sockscripter tools); then
    return 1
  fi
  readonly bin_path
  local cur prev
  cur=${COMP_WORDS[COMP_CWORD]}
  prev=${COMP_WORDS[COMP_CWORD - 1]}
  readonly cur prev

  local flag
  case "$COMP_CWORD" in
  '1')
    flag=-s
    ;;
  '2')
    if $bin_path -p "$prev"; then
      return
    fi
    flag=-c
    ;;
  *)
    if $bin_path -a "$prev"; then
      return
    fi
    flag=-c
    ;;
  esac
  readonly flag
  if [ -n "$cur" ]; then
    mapfile -t COMPREPLY < <(compgen -W "$($bin_path $flag)" -- "$cur")
  else
    mapfile -t COMPREPLY < <(compgen -W "$($bin_path $flag)")
  fi
}

alias sockscripterh='fx sockscripter'
alias sockscripterf='fx shell sockscripter'

complete -F _sockscripter sockscripterh
complete -F _sockscripter sockscripterf
