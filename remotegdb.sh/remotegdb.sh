#!/bin/sh

WORKING_DIR=`pwd`

# this is taken from nkd-gdb
get_build_var_for_abi ()
{
    if [ -z "$GNUMAKE" ] ; then
        GNUMAKE=make
    fi
    $GNUMAKE --no-print-dir -f $ANDROID_NDK_ROOT/build/core/build-local.mk -C $WORKING_DIR DUMP_$1 APP_ABI=$2
}

get_build_var ()
{
    if [ -z "$GNUMAKE" ] ; then
        GNUMAKE=make
    fi
    $GNUMAKE --no-print-dir -f $ANDROID_NDK_ROOT/build/core/build-local.mk -C $WORKING_DIR DUMP_$1
}

adb_shell ()
{
	adb shell $@ | sed -e 's![[:cntrl:]]!!g'
}

if [ -z $ANDROID_NDK_ROOT ]
then
	echo "Please set the \$ANDROID_NDK_ROOT environment variable to the path where the NDK resides."
	exit 1
fi

if [ ! -f $WORKING_DIR/AndroidManifest.xml ]
then
	echo "Please run this script from the project directory (where AndroidManifest.xml is located)."
	exit 1
fi

# first step: get the package name
AWK_DIR=$ANDROID_NDK_ROOT/build/awk
PACKAGE_NAME=`awk -f $AWK_DIR/extract-package-name.awk $WORKING_DIR/AndroidManifest.xml`
echo "Package name is $PACKAGE_NAME"

# second step: use the package name to find the running process id
PID=`adb_shell ps | grep $PACKAGE_NAME | sed -E 's/^([ \t]*)(.*)([ \t]*$)/\2/g;s/([ \t]+)/\ /g;/^$/d' | cut -f 2 -d ' '`
if [ -z "$PID" ]
then
	echo "Please make sure that your program is running. No pid found on the device."
	exit 1
fi

echo "Found running pid: $PID"

# third step: start the remote gdb server. If running, kill it
GDBPID=`adb_shell ps -t | grep gdbserver | sed -E 's/^([ \t]*)(.*)([ \t]*$)/\2/g;s/([ \t]+)/\ /g;/^$/d' | cut -f 2 -d ' '`
if [ ! -z "$GDBPID" ]
then
	adb shell su -c "kill -9 $GDBPID"
	echo "Found running gdbserver session with pid $GDBPID. Killed it."
fi

APP_ABIS=`get_build_var APP_ABI`
COMPAT_ABI="none"
CPU_ABI=`adb_shell getprop ro.product.cpu.abi`
for ABI in $APP_ABIS; do
    if [ "$ABI" = "$CPU_ABI" ] ; then
        COMPAT_ABI=$CPU_ABI
        break
    fi
done

CPU_ABI2=`adb_shell getprop ro.product.cpu.abi2`
if [ -z "$CPU_ABI2" ] ; then
    echo "Device CPU ABI: $CPU_ABI"
else
    echo "Device CPU ABIs: $CPU_ABI $CPU_ABI2"
    if [ "$COMPAT_ABI" = "none" ] ; then
        for ABI in $APP_ABIS; do
            if [ "$ABI" = "$CPU_ABI2" ] ; then
                COMPAT_ABI=$CPU_ABI2
                break
            fi
        done
    fi
fi

APP_OUT=`get_build_var_for_abi TARGET_OUT $COMPAT_ABI`
echo "Using app_out directory: $APP_OUT"

# check out the necessary files
DATA_DIR=`adb_shell run-as $PACKAGE_NAME /system/bin/sh -c pwd`
APP_PROCESS=$APP_OUT/app_process
adb pull /system/bin/app_process $APP_PROCESS
echo "Pulled $APP_PROCESS from device/emulator."
LIB_CDEV=$APP_OUT/libc.so
adb pull /system/lib/libc.so $LIB_CDEV
echo "Pulled $LIB_CDEV from device/emulator."

# now we shall launch the gdbserver on background with root permissions, then
# forward the tcp connection
DEBUG_PORT=5039

adb shell su -c "$DATA_DIR/lib/gdbserver :$DEBUG_PORT --attach $PID" &
echo "Running gdbserver :$DEBUG_PORT --attach $PID"
sleep 10
adb forward tcp:$DEBUG_PORT tcp:$DEBUG_PORT

TOOLCHAIN_PREFIX=`get_build_var_for_abi TOOLCHAIN_PREFIX $COMPAT_ABI`
echo $TOOLCHAIN_PREFIX
GDBSETUP_INIT=`get_build_var_for_abi NDK_APP_GDBSETUP $COMPAT_ABI`
echo $GDBSETUP_INIT
GDBCLIENT=${TOOLCHAIN_PREFIX}gdb
GDBSETUP=$APP_OUT/gdb.setup
cp -f $GDBSETUP_INIT $GDBSETUP
echo "target remote :$DEBUG_PORT" >> $GDBSETUP
if [ -n "$OPTION_EXEC" ] ; then
    cat $OPTION_EXEC >> $GDBSETUP
fi
$GDBCLIENT -x $GDBSETUP -e $APP_PROCESS