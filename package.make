# Define package specific GNU make variables in here.

IN_VARS := SPEW_LEVEL DEBUG

# What to call a tarball file name prefix TAR_NAME-VERSION.tar.gz
TAR_NAME := crts

# package release version.  May differ from interface (library) versions.
VERSION := 0.1

# PACKAGE CXXFLAGS is appended to the users CXXFLAGS
CXXFLAGS := -std=c++11
