HACK(mesch): Until https://github.com/domokit/mojo/issues/817 is fixed for c
bindings too, we cannot place mojoms in directories that contain dashes. C
bindings are created by the go mojom compiler, which is not documented how to
build and deploy, so I cannot change it right now.
