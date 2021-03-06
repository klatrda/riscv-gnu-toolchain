RISC-V GNU Toolchain for PULP
=============================

# Introduction

This is a port of the RISCV GCC toolchain, which has been extended
to support the extensions of the Pulpino core.

# Build

Run the following command to build the toolchain

```
    mkdir build && cd build
    ../riscv-gnu-toolchain/configure --with-xlen=32 --with-arch=IM \
           --disable-atomic --enable-multilib  --prefix=`pwd`/install
    make
```

This will download a specific version of the RISCV toolchain based on gcc 5.4.0,
patch it with extensions for Pulpino and compile it.

The resulting toolchains should be in the install directory.

# Supported cores

This toolchain have multilib support for the following cores:

- pulp:       -march=IMCXpulpslim
- riscy:      -march=IMCXpulpv2
- riscy+fpu:  -march=IMFDCXpulpv2 -mhard-float
- zeroriscy:  -march=IMC
- microriscy: -march=IEC




(The original RISC-V readme follows)


RISC-V GNU Compiler Toolchain
=============================

This is the RISC-V C and C++ cross-compiler. It supports two build modes:
a generic ELF/Newlib toolchain and a more sophisticated Linux-ELF/glibc
toolchain.

### Prerequisites

Several standard packages are needed to build the toolchain.  On Ubuntu,
executing the following command should suffice:

    $ sudo apt-get install autoconf automake autotools-dev curl libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils

On Fedora 22 OS, executing the following command should suffice:
    $ sudo yum install autoconf automake autotools-devel libmpc-devel mpfr-devel gmp-devel gawk  bison flex texinfo patchutils gcc 

On Mac OS, the source and build directories must live in a case-sensitive file
system.  The simplest approach is to create and mount a new disk image with
that property.  Make sure that the mount point does not contain spaces.

This process will start by downloading about 200 MiB of upstream sources, then
will patch, build, and install the toolchain.  If a local cache of the
upstream sources exists in $(DISTDIR), it will be used; the default location
is /var/cache/distfiles.  Your computer will need about 8 GiB of disk space to
complete the process.

### Installation (Newlib)

To build the Newlib cross-compiler, pick an install path.  If you choose,
say, `/opt/riscv`, then add `/opt/riscv/bin` to your `PATH` now.  Then, simply
run the following command:

    ./configure --prefix=/opt/riscv
    make

You should now be able to use riscv-gcc and its cousins.

### Installation (Linux)

To build the Linux cross-compiler, pick an install path.  If you choose,
say, `/opt/riscv`, then add `/opt/riscv/bin` to your `PATH` now.  Then, simply
run the following command:

    ./configure --prefix=/opt/riscv
    make linux

### Installation (Linux multilib)

To build the Linux cross-compiler with support for both 32-bit and
64-bit, run the following commands:

    ./configure --prefix=/opt/riscv --enable-multilib
    make linux

The multilib compiler will have the prefix riscv-unknown-linux-gnu-,
rather than the usual prefix (riscv32-... or riscv64-...).

### Advanced Options

There are a number of additional options that may be passed to
configure.  See './configure --help' for more details.
