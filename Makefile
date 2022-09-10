
# If you're not building this package within Buildroot, then
# you need to uncomment & adapt the following three variables,
# and comment out the existing LDFLAGS variable (since it
# contains $(TARGET_DIR), which is not set outside the
# Buildroot environment.

# BR2 = /path/to/buildroot-x.y.z
# CC = $(BR2)/output/host/bin/arm-buildroot-linux-gnueabihf-gcc
# LDFLAGS = -L$(BR2)/output/target/usr/lib

LDFLAGS = -L$(TARGET_DIR)/usr/lib
LDLIBS = -lVE -lvencoder -lMemAdapter

all: main

main: conf.h cam.o util.o h264.o
h264.o: conf.h util.o
cam.o: conf.h util.o
util.o: conf.h util.h

clean:
	rm -f $(wildcard *.o)
	rm -f main

