stegdetect
==========

Stegdetect is an automated tool for detecting steganographic content in images.

URL: http://www.outguess.org/detection.php

### Building on x86_64

To build on x86_64 you must prefix the commands with `linux32`, like so

```bash
    $ linux32 ./configure
    $ linux32 make
```

You can now run stegdetect from the local directory: `./stegdetect`

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

### License

Copyright 2002 Niels Provos <provos@citi.umich.edu>. See LICENSE for details.
