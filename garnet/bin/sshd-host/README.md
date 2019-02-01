# sshd-host

sshd-host listens on a port (optional argument, default: 22) for TCP
connections and spawns an sshd in inetd mode for each incoming connection.

sshd-host also executes `hostkeygen` once at startup, in order to ensure that
there are host ssh keys available for the sshd process.