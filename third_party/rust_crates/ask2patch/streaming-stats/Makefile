ctags:
	ctags --recurse --options=ctags.rust --languages=Rust

docs:
	cargo doc
	# WTF is rustdoc doing?
	in-dir ./target/doc fix-perms
	rscp ./target/doc/* gopher:~/www/burntsushi.net/rustdoc/

push:
	git push origin master
	git push github master

