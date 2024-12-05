CFLAGS=`pkgconf --cflags --libs libpjproject`

all: call

wav_reader: wav_reader.c
	gcc -o $@ $<

wav.c: wav_reader ring.wav
	./wav_reader ring.wav wav.c

call: call.c config.h wav.c
	gcc -o $@ $< $(CFLAGS)

install:
	install call /usr/local/bin

uninstall:
	rm /usr/local/bin/call

clean:
	rm -f call wav.c wav_reader
