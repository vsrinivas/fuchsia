GPG keyring file generated using:

```shell
#     wheezy   jessie   jessie-stable stretch  buster
KEYS="46925553 2B90D010 518E17E1      1A7B6500 3CBBABEE"
gpg --keyserver keyserver.ubuntu.com --recv-keys $KEYS
gpg --output ./debian-archive-keyring.gpg --export $KEYS
```
