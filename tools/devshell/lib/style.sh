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
#   style::cat --color black --background cyan --indent 4 <<EOF
#   Multi-line text with expanded bash ${variables}
#   can be styled and indented.
#   EOF
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
# Visual tests (and demonstration of capabilities) can be run from:
#   //scripts/tests/style-test-visually

# This script should be sourced. It is compatible with Bash 3.
# MacOS still comes with Bash 3, so unfortunately no associative arrays.

STYLE_TO_TTY_ONLY=false  # Set to true to suppress styling if output is redirected

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

style::colors() {
  set | sed -n "s/^TERM_COLORS__\([^=]*\)=.*$/\1/p" >&2
}

style::attributes() {
  set | sed -n "s/^TERM_ATTRIBUTES__\([^=]*\)=.*$/--\1/p" >&2
}

style::usage() {
  local help_option="$1"; shift
  if [[ "${help_option}" == "colors" ]]; then
    style::colors
    return
  elif [[ "${help_option}" == "attributes" ]]; then
    style::attributes
    return
  fi
  local function_call="$1"
  local -a words=( $function_call )
  local funcname="${words[0]}"
  local command="$2"
  local specifics="$3"

  >&2 echo "
Usage: ${function_call} [style options] [command parameters]"

  if [[ "${specifics}" != "" ]]; then
    >&2 echo "
${specifics}"
  fi
  >&2 cat << EOF

style options include:
  --bold, --faint, --underline, etc.
  --color <color_name>
  --background <color_name>
  --indent <spaces_count>
  --stderr (output to standard error instead of standard out)

  echo "This is \$(style::echo -f --bold LOUD) and soft."

command parameters are those supported by the ${command} command.

Use ${funcname} --help colors for a list of colors or backgrounds
Use ${funcname} --help attributes for a list of style attribute flags
EOF
}

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
  echo "${attribute}"
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
  echo "${color}"
}

style::background() {
  local color
  color=$(style::color "$1" "$2" || exit $?) || return $?
  echo $((10+${color}))
}

style::stylize() {
  if [[ "$1" == --* || "$1" == "" ]]; then
    style::usage "$2" "${FUNCNAME[0]} <command>" "stylized" "\
<command> is any command with output to stylize, followed by style options,
and then the command's normal parameters."
    return
  fi

  local command="$1"; shift
  if [[ "$1" == "--help" ]]; then
    style::usage "$2" "style::${command}" "'${command}'"
    return
  fi

  local get_flags=true
  local -i fd=1
  local styles
  local semicolon
  local name
  local -i indent=0
  local prefix
  local -i code=0

  while $get_flags; do
    case "$1" in
      --stderr)
        fd=2
        shift
        ;;
      --stdout)
        fd=1
        shift
        ;;
      --color)
        shift; name="$1"; shift
        styles="${styles}${semicolon}$(style::color "$name" || exit $?)" || return $?
        semicolon=';'
        ;;
      --background)
        shift; name="$1"; shift
        styles="${styles}${semicolon}$(style::background "$name" || exit $?)" || return $?
        semicolon=';'
        ;;
      --indent)
        shift; indent=$1; shift
        prefix="$(printf "%${indent}s")"
        ;;
      --*)
        name="${1:2}"
        code=$(style::attribute "$name" 0)
        if (( code )); then
          shift
          styles="${styles}${semicolon}${code}"
          semicolon=';'
        else
          code=$(style::color "$name" 0)
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

  if [ ! -t ${fd} ] && ${STYLE_TO_TTY_ONLY}; then
    # Output is not to a TTY so don't stylize
    if [[ "${prefix}" == "" ]]; then
      >&${fd} "${command}" "$@" || status=$?
    else
      >&${fd} "${command}" "$@" | sed "s/^/${prefix}/"
      if (( ${PIPESTATUS[0]} != 0 )); then
        status=${PIPESTATUS[0]}
      fi
    fi
    return 0
  fi

  local if_newline=''
  local text

  # Add placeholder (.) so command substitution doesn't strip trailing newlines
  text="$("${command}" "$@" || exit $?;echo -n '.')" || return $?
  if [[ "${prefix}" != "" ]]; then
    text="$(echo "${text}" | sed "s/^/${prefix}/;\$s/^${prefix}[.]\$/./")"
  fi

  local -i len=$((${#text}-2))
  if [[ "${text:$len:1}" == $'\n' ]]; then
    # Save last newline to add back after styling.
    if_newline='\n'
  else
    ((len++))
  fi
  # Strip trailing newline, if any, and placeholder.
  text="${text:0:$((len))}"

  # Style everything except newlines, otherwise background color highlights
  # entire line. Add extra line with a character so sed does not add it's own
  # last newline, then delete the line after substitutions.
  local styled=$(printf '%s\n.' "${text}" | sed -e $'s/$/\033[0m/;s/^/\033['"${styles}"'m/;$d')

  >&${fd} printf "%s${if_newline}" "${styled}"

  return 0
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

style::_echo_with_styles() {
  local funcname="$1";shift
  local style_options="$1";shift
  if [[ "$1" == "--help" ]]; then

    style::usage "$2" "${funcname}" "echo" "\
Default style options for ${funcname}:
  $(style::echo "${style_options}" --stdout \""${style_options}"\")"

    return
  fi
  style::echo "${style_options}" "$@" || return $?
}

style::error() {
  style::_echo_with_styles "${FUNCNAME[0]}" "${STYLE_ERROR}" "$@" || return $?
}

style::warning() {
  style::_echo_with_styles "${FUNCNAME[0]}" "${STYLE_WARNING}" "$@" || return $?
}

style::info() {
  style::_echo_with_styles "${FUNCNAME[0]}" "${STYLE_INFO}" "$@" || return $?
}

style::link() {
  style::_echo_with_styles "${FUNCNAME[0]}" "${STYLE_LINK}" "$@" || return $?
}
