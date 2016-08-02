all: example-session.png example-footprint.png example-footprint-changed.png

%.png : %.dot
	dot -Tpng $< > $@
