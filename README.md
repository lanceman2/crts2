## Ports

Currently developing on: 
  - Debian GNU/Linux 9.3 (stretch) and
  - Debian GNU/Linux 8.10 (jessie)

## Dependencies


```
apt-get install build-essential libuhd-dev libreadline-dev graphviz imagemagick
```

In addition, this may not be needed, on broken Ubuntu systems that mess up
the libuhd-dev installation:
```
apt-get install libboost-all-dev
```

Maybe more...


## Installing from git clone source

In the top source directory run

```
./bootstrap
```
which will download the file quickbuild.make.

Then edit the file Devel_Configure, making sure you set
PREFIX (the installation prefix) to the directory that you
would like, and then run it:
```
./Devel_Configure
```
which will generate the one file config.make.

Then run
```
make download
```
which will download the liquid-dsp and libfec package tarballs.
At this point you have all the needed files "non-system installed"
files.  So ya, no more downloads unless you need to install a
system dependency.

Then run
```
make
```
and then run
```
make install
```

You could run all these commands in one line, but things can
happen, like not having a dependency install, or a server
is not serving a downloaded file.


## Tests


###  FFT monitor, urandom transmitter, receiver to hexdump

```
cd bin

./termRun

./termRun uhd_fft -f 915.0e6 -s 10.5e6 --args addr=192.168.10.3

./termRun "cat /dev/urandom |\
 ./crts_radio\
 -f stdin\
 -f liquidFrame\
 -f tx [ --uhd addr=192.168.10.2 --freq 915.5 ]"

./termRun "./crts_radio\
 -f rx [ --uhd addr=192.168.10.4 --freq 915.5 ]\
 -f liquidSync\
 -f stdout |\
 hexdump -v"
```

