#!/bin/bash

PWD=`pwd`
ARCH="arm64"
CROSS_COMPILE="aarch64-linux-gnu-"
KVER=`make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE kernelversion`
BUILD_OUT=`pwd`/../out_${ARCH}_$KVER
NP=`nproc --all`
CONFIG=debian_defconfig

usage() {
	echo "Usage: $0 <arg>"
	echo "arg:"
	echo "  all: build all targets and install them."
	echo "  image: build the bzImage."
	echo "  menuconfig: reconfig the kernel."
	echo "  modules: build the modules."
}

build_all() {
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT $CONFIG 2>&1 | tee $BUILD_OUT/make_config.log
#	bear -- make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT -j $NP || exit
#	bear -- make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT -j $NP 2>&1 | tee $BUILD_OUT/make_all.log
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT -j $NP 2>&1 | tee $BUILD_OUT/make_all.log
}

make_image() {
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT $CONFIG 2>&1 | tee $BUILD_OUT/make_config.log
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT Image -j $NP 2>&1 | tee $BUILD_OUT/make_image.log
}

make_menuconfig() {
	echo "reconfig kernel"
#	if [ ! -f $BUILD_OUT/.config ]; then
		make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT $CONFIG 2>&1 | tee $BUILD_OUT/make_config.log
#	fi
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT menuconfig
	cp -f $BUILD_OUT/.config $PWD/arch/$ARCH/configs/$CONFIG
}

make_modules() {
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT $CONFIG 2>&1 | tee $BUILD_OUT/make_config.log
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$BUILD_OUT modules -j $NP 2>&1 | tee $BUILD_OUT/make_modules.log
}

if [ $# -lt 1 ]; then
	usage
	exit 1
fi

case $1 in
	all)
		build_all
		;;

	image)
		make_image
		;;

	menuconfig)
		make_menuconfig
		;;

	modules)
		make_modules
		;;
	*)
		usage
		;;
esac

exit 0

