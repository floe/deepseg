#!/bin/bash

VERSION=20241106140722

##
## https://github.com/floe/backscrub
##
## backscrub, Fedora build script
## Roman E. Chechnev <m0r1k>
##

ROOT_DIR=~/Downloads/backscrub_patched/
BACKSCRUB_GIT_DIR=$ROOT_DIR/backscrub/
BACKSCRUB_BUILD_DIR=$BACKSCRUB_GIT_DIR/build/
CPU_CORES=$(grep -c processor /proc/cpuinfo)
CAPTURE_DEV=/dev/video1
VIRT_DEV_ID=0
VIRT_DEV=/dev/video$VIRT_DEV_ID
TARGET=/usr/local/bin/backscrub
BACK_GROUND=$BACKSCRUB_GIT_DIR/backgrounds/rotating_earth.webm
ACTION=$1

set -ue;

function do_checks
{
    RES=`cat /etc/*lease | grep -i fedora || /bin/true`;
    if [ ! -n "$RES" ];then
        echo "sorry, only Fedora supported now";
        exit 1;
    fi
}

function do_clone
{
    cd $ROOT_DIR;
    git clone --recursive --depth=1 https://github.com/floe/backscrub.git;
}

function do_cmake
{
    cd $BACKSCRUB_BUILD_DIR;
    cmake ../;
}

function do_patch
{
    cd $BACKSCRUB_BUILD_DIR;
    find _deps/abseil-cpp-build/ -type f -name '*.make' \
        | xargs sed -i 's/c++14/c++23/g'
    find $BACKSCRUB_BUILD_DIR/_deps/flatbuffers-* -type f -name '*.make' \
        | xargs sed -i 's/-pedantic -Werror -Wextra -Werror=shadow//g';
    find $BACKSCRUB_BUILD_DIR/tensorflow-lite/    -type f -name '*.make' \
        | xargs sed -i 's/c++14/c++23/g';
}

function do_build
{
    cd $BACKSCRUB_BUILD_DIR;
    make -j$CPU_CORES;
}

function do_install
{
    if [ ! -r "$TARGET" ]; then
        cd $BACKSCRUB_BUILD_DIR;
        sudo make install;
    fi
}

function do_setup
{
    FNAME=/etc/modprobe.d/v4l2loopback.conf
    if [ ! -r "$FNAME" ]; then
        sudo cat <<EOF > $FNAME
# V4L loopback driver
options v4l2loopback max_buffers=2
options v4l2loopback exclusive_caps=1
options v4l2loopback video_nr=$VIRT_DEV_ID
options v4l2loopback card_label="WebCam"
EOF
    fi

    FNAME=/etc/modules-load.d/v4l2loopback.conf
    if [ ! -r "$FNAME" ]; then
        sudo cat <<EOF > $FNAME
v4l2loopback
EOF
    fi
}

function do_modprobe
{
    sudo modprobe v4l2loopback;
}

function do_start
{
    echo "you can use ffplay /dev/$VIRT_DEV to play video";
    backscrub               \
        -c $CAPTURE_DEV     \
        -v $VIRT_DEV        \
        -b $BACK_GROUND     \
        --cg 1920x1080      \
        --vg 1920x1080      \
        -f MJPG
}

function do_usage
{
    echo "`basename $0` <action>";
    echo "actions:";
    echo " build - to build and install"
    echo " start - to start"
    echo " help  - to this help"
}

## main

if [ "$ACTION" == "build" ]; then
    do_checks;

    mkdir -p $ROOT_DIR;
    cd $ROOT_DIR;
    if [ ! -d "$BACKSCRUB_GIT_DIR" ]; then
        do_clone;
    fi

    if [ ! -d "$BACKSCRUB_BUILD_DIR" ]; then
        mkdir $BACKSCRUB_BUILD_DIR;
        do_cmake;
        do_patch;
    fi

    do_build;
    do_install;
    do_setup;
    do_modprobe;
    do_start;
elif [ "$ACTION" == "start" ]; then
    do_modprobe;
    do_start;
else
    do_usage;
fi

