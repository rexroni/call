CFLAGS=`pkgconf --cflags --libs libpjproject`

all: call simple_pjsua

call: call.c config.h
	gcc -o $@ $< $(CFLAGS)

simple_pjsua: simple_pjsua.c
	gcc -o $@ $< $(CFLAGS)

install:
	install call /usr/local/bin

clean:
	rm call simple_pjsua
