#!/bin/sh

# This script runs commands necessary to generate a Makefile for sng.

echo "Warning: This script will run configure for you -- if you need to pass"
echo "  arguments to configure, please give them as arguments to this script."

aclocal
autoheader
automake --add-missing
autoconf
automake

./configure $*

exit 0
