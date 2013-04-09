stegdetect
==========

Stegdetect is an automated tool for detecting steganographic content in images.

URL: http://www.outguess.org/detection.php

This repo contains the 0.6 sources with compilation fixes for modern dev
environments and Android (ARM).

### Building on x86_64

To build on x86_64 you must prefix the commands with `linux32`, like so

```bash
    $ linux32 ./configure
    $ linux32 make
```

You can now run stegdetect from the local directory: `./stegdetect`

### Build for Android

This Android build is sort of fragile, there are many compile warnings.

```bash
    $ make clean
    $ make -f Makefile.android
```

### Usage

Stegdetect does two types of F5 detection, simple and "slow". Simple merely
looks for a comment in the header, which any savy coder has removed. This
method is enabled with the "`-tf`" option.

The "slow" F5 detection is enabled with "`-tF`"

To run detection on a directory of JPEGs use:

```bash
    for img in `find /path/to/images/ -iname "*jpg"`; do
        ./stegdetect -tF $img;
    done
```

**Android**


```bash
    $ adb push stegdetect /data/local/tmp
    $ adb shell
        for img in `ls /sdcard/PixelKnot/*jpg`; do
            /data/local/tmp/stegdetect -tF $img
        done
```

### License

Copyright 2002 Niels Provos <provos@citi.umich.edu>. See LICENSE for details.
