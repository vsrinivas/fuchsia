#!/bin/sh
#
# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# This script is run within the chroot of the created Debian distribution.

export DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true
export LC_ALL=C LANGUAGE=C LANG=C

# Abort on error.
set -e

# Create default account.
username="root"
default_password="password"
home=/root
echo "${username}:${default_password}" | chpasswd
echo "Default login/password is ${username}:${default_password}" > /etc/issue

# Squelch MOTD.
touch ${home}/.hushlogin

# Make the prompt as simple as possible (useful for testing).
echo "PS1='$ '" >> ${home}/.profile

# Setup hostname.
echo "machina-guest" > /etc/hostname
echo "127.0.1.1    machina-guest" >> /etc/hosts

# Prevent certain known-bad modules from loading.
#
# https://fuchsia.dev/fuchsia-src/contribute/respectful_code note:
# "blacklist" is a keyword required by modprobe.
cat > /etc/modprobe.d/machina.conf << EOF
# Causes a panic on certain x86-64 CPUs: <http://fxbug.dev/86351>.
blacklist intel_pmc_core
EOF

# Add some modules to the initramfs. The console and GPU are useful to have
# before the rootfs mounts in case things go off the rails.
cat >> /etc/initramfs-tools/modules << EOF
virtio_console
virtio_blk
virtio_gpu
virtio_input
EOF

# Explicitly disable resume from swap. This is to ensure we never try to to
# wait for a resume device before finishing booting since we don't use a
# swap partition anyways.
echo "RESUME=none" > /etc/initramfs-tools/conf.d/resume

update-initramfs -u

# Enable automatic login for serial getty, most importantly on hvc0. This
# overrides the default configuration at
# /lib/systemd/system/serial-getty@.service. The first ExecStart line is to
# reset in the case where the default configuration is set up to append
# ExecStart lines. Note: We use `--skip-login --login-options "-f ${username}"`
# instead of `--autologin` to make agetty quieter.
mkdir -p /etc/systemd/system/serial-getty@.service.d
cat >> /etc/systemd/system/serial-getty@.service.d/override.conf << EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --skip-login --login-options "-f ${username}" --noissue --noclear %I xterm
EOF

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

# Expose the guest interaction daemon to allow automated tests to send files to
# and receive files from the guest and exec commands.
cat >> /etc/systemd/system/guest_interaction_daemon.service << EOF
[Unit]
Description=Guest Interaction Daemon

[Service]
Type=simple
ExecStart=-/guest_interaction/guest_interaction_daemon

[Install]
WantedBy=multi-user.target
EOF

systemctl enable telnet.socket
systemctl enable guest_interaction_daemon.service

# Mount the test utils and guest interaction daemon on start up.
cat >> /etc/fstab << EOF
/dev/vdb /test_utils auto ro 0 0
/dev/vdc /guest_interaction romfs ro 0 0
EOF

# Remove files cached by apt.
apt clean
