#!/bin/sh 
#
# requires: 
#  expect
#  cvs (if pulling from CVS

rootdir=/tmp/pvfs2-build-test
tarballurl=http://www.mcs.anl.gov/hpio/pvfs2-0.0.6.tar.gz
cvsroot=:pserver:anonymous@cvs.parl.clemson.edu:/anoncvs 
# specify extra configure options here; for now we disable karma because
# of all the gtk warnings
configureopts=--disable-karma


#
# use this method if you want to test a release
#   takes no arguments.  returns nonzero on error
get_dist() {
	# get the source (-nv keeps it kinda quiet)
	wget -nv $tarballurl

	if [ $? != 0 ] ; then
		echo "wget of $tarballurl failed.  Aborting."
		exit 1
	fi
	# untar the source
	tar xzf $tarball

	if [ -d $tarballdir ] ; then
		mv $tarballdir $srcdir
	fi

	if [ ! -d $srcdir ] ; then
		echo "Tarball $tarball did not create a $srcdir directory or a $tarballdir directory.  Aborting."
		exit 1
	fi

}

# get_cvs requires expect
# use this method if you want to, well, test whatever is in cvs at this very
# moment.  takes no arguments. returns nonzero on error.
get_cvs() {
	expect -c "spawn -noecho cvs -Q -d $cvsroot login; send \r;"
	cvs -Q -d $cvsroot co pvfs2
	if [ $? -ne 0 ] ; then
		echo "Pulling PVFS2 from $cvsroot failed."
		exit 1
	fi
}

# end of user defines

tarball=`basename $tarballurl`
tarballdir=`echo $tarball | sed -e "s/.tar.gz//" | sed -e "s/.tgz//"`
old_wd=`pwd`
build_kernel="false"
kerneldir=""

usage()
{
    echo "USAGE: pvfs2-build.sh <-k kernel source> <-r dir>"
    echo "  -k: path to kernel source (enables module build)"
    echo "  -r: path to directory to build and install in"
    return
}

# get command line arguments
while getopts k:r: opt
do
    case "$opt" in
	k) build_kernel="true"; kerneldir="$OPTARG";;
	r) rootdir="$OPTARG";;
	\?) usage; exit 1;; 
    esac
done   

echo "PVFS2 will be built in ${rootdir}."

if [ ! -d $rootdir ] ; then
	echo "Specified directory $rootdir does not exist.  Aborting."
	exit 1
fi

date=`date "+%Y-%m-%d-%H-%M"`
host=`uname -n`

srcdir=$rootdir/pvfs2
builddir=$rootdir/BUILD-pvfs2
installdir=$rootdir/INSTALL-pvfs2

# clean up src, build, install directories
# clean up misc. files in the root directory too
rm -rf $srcdir $builddir $installdir
rm -rf $rootdir/\*.tgz
rm -rf $rootdir/pvfs2

# move to our root dir
cd $rootdir

# could make this some sort of command line option... 
get_cvs || exit 1


# create build and install directories, configure
mkdir $builddir
mkdir $installdir
cd $builddir
if [ $build_kernel == "true" ] ; then
	$srcdir/configure $configureopts --with-kernel=$kerneldir --prefix=$installdir 2>&1 > $rootdir/configure.log
else
	$srcdir/configure $configureopts --prefix=$installdir 2>&1 > $rootdir/configure.log
fi

if [ $? != 0 ] ; then
	echo "Configure failed; see $rootdir/configure.log.  Aborting."
	exit 1
fi

# make
make 2>&1 > $rootdir/make.log

if [ $? != 0 ] ; then
	echo "Make failed; see $rootdir/make.log.  Aborting."
	exit 1
fi

# look through make output
PEMM=`which pvfs2-extract-make-msgs.pl 2>/dev/null`
if [ x$PEMM == "x" ] ; then
	if [ ! -x $old_wd/pvfs2-extract-make-msgs.pl ] ; then
		echo "Failed to find pvfs2-extract-make-msgs.pl.  Aborting."
		exit 1
	else
		PEMM=$old_wd/pvfs2-extract-make-msgs.pl 
	fi
fi
$PEMM $rootdir/make.log 2>&1 > $rootdir/make-extracted.log

if [ $? != 0 ] ; then
	echo "Spurious output during make; see $rootdir/make-extracted.log.  Aborting."
	exit 1
fi

# make kernel stuff if necessary
if [ $build_kernel == "true" ] ; then
	make kmod 2>&1 >> $rootdir/make.log
fi

if [ $? != 0 ] ; then
	echo "Make kmod failed; see $rootdir/make.log.  Aborting."
	exit 1
fi

# make install
make install 2>&1 > $rootdir/make-install.log

if [ $? != 0 ] ; then
	echo "Make install failed; see $rootdir/make-install.log.  Aborting."
	exit 1
fi

# not sure where to put this; just go with sbin for now?
if [ $build_kernel == "true" ] ; then
	cp $builddir/src/kernel/linux-2.6/pvfs2.ko $installdir/sbin/
fi

exit 0
