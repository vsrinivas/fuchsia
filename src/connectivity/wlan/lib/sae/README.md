# WLAN SAE

This library provides a general implementation of Simultaneous Authentication of Equals.

SAE is used for symmetric, observation-proof authentication in mesh and WPA3 networks. SAE handshakes are encapsulated in IEEE 802.11 authentication frames, which should be parsed by the client of this library.

Although SAE can technically be performed with many different cryptographic groups, this library currently only supports one particular elliptic curve group that is widely adopted. BoringSSL is used for all underlying crypto operations to prevent most timing attacks/crypto errors.