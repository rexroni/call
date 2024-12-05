# `call`

Make and receive calls from your computer.

## Usage

To make a call, provide the phone number as argument(s) to `call`.  All spaces
and special characters are ignored, so the following are equivalent:
- `call +1 234.567.8910`
- `call 12345678910`

To receive a call, just run `call` without any arguments.  When a call arrives,
`ring.wav` will sound on your speakers, then `call` will auto-answer.

Within a call, you can type `0-9`, `#`, and `*` for the usual touch-tone
behavior.

## System Requirements

`call` only works on Linux right now.

It probably isn't too hard to get working on either macos or windows, since
the only build dependency is libpjproject, which is cross-platform.

## Configuration

`call` is configured with a `config.h` that you must write yourself.  See
`config-udp-example.h` and `config-tls-example.h` for examples that work with
my voip provider, [voip.ms](https://voip.ms).

I recommend getting the UDP transport working first.  Using TLS may require
steps with your sip provider.  For example, voip.ms has [these steps](
https://wiki.voip.ms/article/Call_Encryption_-_TLS/SRTP).

## Build

Make sure you have libpjproject installed.  Then just run `make`.

## Install

Just run `sudo make install`.

## Uninstall

Just run `sudo make uninstall`.

## Customizing ring.wav

If you want to change the audio that gets played when an incoming call arrives,
you can use `ffmpeg` to convert an arbitrary audio file into a suitable
ring.wav:

    ffmpeg -i my-ring.mp3 -c pcm_s16le ring.wav

Then just run `make` again.

## Additional links

- [Wavefom Audio File Format specification](
    https://www.aelius.com/njh/wavemetatools/doc/riffmci.pdf
) (page 56)
- [ring.wav original audio source](
https://pixabay.com/sound-effects/rotary-phone-ring-medium-103869)
- [ring.wav original audio license](
https://pixabay.com/service/license-summary)
