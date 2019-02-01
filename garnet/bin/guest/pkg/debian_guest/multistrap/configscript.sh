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

# Add some modules to the initramfs. The console and GPU are useful to have
# before the rootfs mounts in case things go off the rails.
cat >> /etc/initramfs-tools/modules << EOF
virtio_console
virtio_blk
virtio_gpu
EOF

update-initramfs -u

# Expose a simple telnet interface over vsock port 23.
#
# Note we're using socat to bind the pty to the socket so that we ensure we
# don't send any telnet control messages.
cat >> /etc/systemd/system/telnet.socket << EOF
[Unit]
Description=Telnet Server Activation Port

[Socket]
ListenStream=vsock::23
Accept=true

[Install]
WantedBy=sockets.target
EOF

cat >> /etc/systemd/system/telnet@.service << EOF
[Unit]
Description=Telnet Server
After=local-fs.target

[Service]
ExecStart=-/usr/bin/socat - EXEC:/bin/login,pty,stderr,setsid,sigint,sane,ctty
StandardInput=socket
StandardOutput=socket
EOF

systemctl enable telnet.socket

apt clean
