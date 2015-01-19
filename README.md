# MJPEGView

MJPEGView is an imaginatively-named suite of programs to decode and view MJPEG streams on Linux.
It consists of a loosely-coupled set of modules that can be compiled in different ways to get different programs, ranging from graphical viewers to commandline dumper utilities.

- The `mjpegview` binary is a multithreaded viewer that displays multiple streams.
- The `mjvsimple` binary decodes a single MJPEG stream to disk.
- the `mjvmulti` binary decodes multiple MJPEG streams to disk.

## Config

MJPEGview uses [libconfig](http://www.hyperrealm.com/libconfig) to read and parse its config file.

## Tests

Some tests are included for some modules, which are run automatically by Travis CI.

[![Build Status](https://travis-ci.org/aklomp/mjpegview.png?branch=master)](https://travis-ci.org/aklomp/mjpegview)
