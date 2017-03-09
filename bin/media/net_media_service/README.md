# Net Media Service

net_media_service exposes a MediaPlayer wrapper (called NetMediaPlayer) that's
suitable for remoting. It also exposes a proxy (NetMediaPlayerNetProxy) that
may be used to remotely control a NetMediaPlayer.

NetMediaPlayer accepts URLs and opens them (by creating SeekingReaders). For
this reason, it is intended to run in a user context whose access to network
services and files on disk is limited by the user's permissions.
