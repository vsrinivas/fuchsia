#!/bin/sh
#
# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

export DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true
export LC_ALL=C LANGUAGE=C LANG=C
/var/lib/dpkg/info/dash.preinst install
dpkg --configure -a

# Create default account.
username="bench"
default_password="password"
useradd ${username} -G sudo
echo "${username}:${default_password}" | chpasswd
echo "Default login/password is ${username}:${default_password}" > /etc/issue

# Configure user account.
user_home=/home/${username}
mkdir -p ${user_home}
chown -R ${username}:${username} ${user_home}
chsh -s /bin/bash ${username}

# Setup hostname.
echo "machina-guest" > /etc/hostname
echo "127.0.1.1    machina-guest" >> /etc/hosts

apt clean
