# Installation Instructions

randio uses the Meson build system (see http://mesonbuild.com/).
This means that in order to install randio the following will work:

    meson build && cd build && ninja && ninja install

Meson will tell you if there are any missing dependencies. In order
to install as a user, use "mesonconf" to set the prefix with:

    mesonconf -Dprefix=$HOME/.local/

To build and install as a user, you thus have to run:

    meson build && cd build && mesonconf -Dprefix=$HOME/.local && ninja && ninja install
