#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

export FUCHSIA_VBOX_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$FUCHSIA_VBOX_SCRIPT_DIR/env.sh"

usage() {
  cat >&2 <<END
Usage: $(basename $0) build-disk|create|console|start|off|stop|delete|reset|debugvm|help

See $FUCHSIA_SCRIPTS_DIR/vbox/env.sh for configurable environment variables.
END
}

case "$1" in
  # Larger commands are in dedicated script files
  build-disk|create|console)
    cmd=$1
    shift
    exec "${FUCHSIA_VBOX_SCRIPT_DIR}/cmds/${cmd}.sh" "$@"
    ;;

  start)
    shift
    if [[ $(uname) == "Linux" && -z $DISPLAY ]]; then
      VBoxHeadless -s "${FUCHSIA_VBOX_NAME}" "$@"
    else
      VBoxManage startvm "${FUCHSIA_VBOX_NAME}" "$@"
    fi
    ;;
  off|stop)
    shift
    VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" poweroff "$@"
    ;;
  delete)
    VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" poweroff
    VBoxManage unregistervm "${FUCHSIA_VBOX_NAME}" --delete
    ;;
  reset)
    shift
    VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" reset "$@"
    ;;

  ctrl|control)
    shift
    VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" "$@"
    ;;

  debugvm)
    shift
    VBoxManage debugvm "${FUCHSIA_VBOX_NAME}" "$@"
    ;;

  -h|--help|help|*)
    usage
    ;;
esac
