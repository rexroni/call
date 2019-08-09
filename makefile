CFLAGS=`pkgconf --cflags --libs libpjproject`

all: call

call: call.c config.h
	gcc -o $@ $< $(CFLAGS)

install:
	install call /usr/local/bin

clean:
	rm call
