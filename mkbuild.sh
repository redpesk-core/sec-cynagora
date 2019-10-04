#/bin/sh

set -e
h=$(dirname $0)
mkdir -p $h/build 
cd $h/build
[ "$1" = "-f" ] && rm -r *
cmake \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_INSTALL_PREFIX=~/.local \
	-DWITH_SYSTEMD=ON \
	-DWITH_CYNARA_COMPAT=ON \
	-DDIRECT_CYNARA_COMPAT=ON \
	..
make -j

