# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script adds text style options to echo, cat, and printf. For
# example:
#
#   style::echo --stderr --bold --underline --color red -n Yo ho ho
#
# print "Yo ho ho" without a newline (echo's -n flag), in bold red
# with underline. The text is written to stderr instead of the default
# stdout.
#
# style::info, style::warning, and style::error use echo to stderr
# with default color and bold text. For example:
#
#   style::warning "WARNING: This is a warning!"
#
# style::link uses echo to stdout, with dark_blue underlined text
#
# You can override these styles with your own preferences, for example:
#
#   export STYLE_WARNING="--stderr --faint --dark_red --background dark_yellow"
#

# This script should be sourced. It is compatible with Bash 3.
# MacOS still comes with Bash 3, so unfortunately no associative arrays.

[[ "${STYLE_ERROR}" != "" ]] || STYLE_ERROR="--stderr --bold --color red"
[[ "${STYLE_WARNING}" != "" ]] || STYLE_WARNING="--stderr --bold --color dark_yellow"
[[ "${STYLE_INFO}" != "" ]] || STYLE_INFO="--stderr --bold --color dark_green"
[[ "${STYLE_LINK}" != "" ]] || STYLE_LINK="--underline --color dark_blue"

declare -i TERM_ATTRIBUTES__reset=0
declare -i TERM_ATTRIBUTES__bold=1
declare -i TERM_ATTRIBUTES__faint=2
declare -i TERM_ATTRIBUTES__italic=3
declare -i TERM_ATTRIBUTES__underline=4
declare -i TERM_ATTRIBUTES__blink=5

declare -i TERM_COLORS__default=39
declare -i TERM_COLORS__black=30
declare -i TERM_COLORS__dark_red=31
declare -i TERM_COLORS__dark_green=32
declare -i TERM_COLORS__dark_yellow=33
declare -i TERM_COLORS__dark_blue=34
declare -i TERM_COLORS__dark_magenta=35
declare -i TERM_COLORS__purple=35
declare -i TERM_COLORS__dark_cyan=36
declare -i TERM_COLORS__light_gray=37
declare -i TERM_COLORS__gray=90
declare -i TERM_COLORS__red=91
declare -i TERM_COLORS__green=92
declare -i TERM_COLORS__yellow=93
declare -i TERM_COLORS__blue=94
declare -i TERM_COLORS__magenta=95
declare -i TERM_COLORS__pink=95
declare -i TERM_COLORS__cyan=96
declare -i TERM_COLORS__white=97

style::attribute() {
  local name="$1"
  local fallback="$2"
  local var=TERM_ATTRIBUTES__${name}
  local -i attribute=${!var}
  if ! (( attribute )); then
    if [[ $fallback != "" ]]; then
      echo "${fallback}"
      return 0
    else
      >&2 echo "Invalid attribute name: $name"
      return 1
    fi
  fi
  echo ${attribute}
}

style::color() {
  local name="$1"
  local fallback="$2"
  local var=TERM_COLORS__${name}
  local -i color=${!var}
  if ! (( color )); then
    if [[ $fallback != "" ]]; then
      echo "${fallback}"
      return 0
    else
      >&2 echo "Invalid color name: $name"
      return 1
    fi
  fi
  echo ${color}
}

style::background() {
  local color
  color=$(style::color "$1" "$2" || exit $?) || return $?
  echo $((10+${color}))
}

_STYLE_RESET="\033[0m"

style::stylize() {
  local command="$1"; shift

  local get_flags=true
  local -i fd=1
  local styles
  local semicolon
  local name
  local -i code

  while $get_flags; do
    case "$1" in
      --stderr)
        fd=2
        shift
        ;;
      --color)
        shift; name="$1"; shift
        styles="${styles}${semicolon}$(style::color $name || exit $?)" || return $?
        semicolon=';'
        ;;
      --background)
        shift; name="$1"; shift
        styles="${styles}${semicolon}$(style::background $name || exit $?)" || return $?
        semicolon=';'
        ;;
      --*)
        name="${1:2}"
        code=$(style::attribute $name 0)
        if (( code )); then
          shift
          styles="${styles}${semicolon}${code}"
          semicolon=';'
        else
          code=$(style::color $name 0)
          if (( code )); then
            shift
            styles="${styles}${semicolon}${code}"
            semicolon=';'
          else
            get_flags=false
          fi
        fi
        ;;
      *)
        get_flags=false
        ;;
    esac
  done

  local status=0

  if [ ! -t $fd ]; then
    # Output is not to a TTY so don't stylize
    >&${fd} "${command}" "$@" || status=$?
    return $status
  fi

  local if_newline=''
  local text
  # Add placeholder (.) so command substitution doesn't strip trailing newlines
  text="$("${command}" "$@" || exit $?;echo '.')" || status=$?
  local -i len=$((${#text}-2))
  if [[ "${text:$len:1}" == $'\n' ]]; then
    if_newline='\n'
  else
    ((len++))
  fi
  # Strip trailing newline, if any, and placeholder
  # Last newline should not be stylized.
  # TODO(richkadel): We may want to remove style from all newlines.
  # Background color looks odd when newlines are styled.
  text="${text:0:$((len))}"

  >&${fd} printf "\033[${styles}m%s${_STYLE_RESET}${if_newline}" "${text}"

  return $status
}

style::echo() {
  style::stylize "${FUNCNAME[0]:7}" "$@" || return $?
}

style::cat() {
  style::stylize "${FUNCNAME[0]:7}" "$@" || return $?
}

style::printf() {
  style::stylize "${FUNCNAME[0]:7}" "$@" || return $?
}

style::error() {
  style::echo ${STYLE_ERROR} "$@" || return $?
}

style::warning() {
  style::echo ${STYLE_WARNING} "$@" || return $?
}

style::info() {
  style::echo ${STYLE_INFO} "$@" || return $?
}

style::link() {
  style::echo ${STYLE_LINK} "$@" || return $?
}
