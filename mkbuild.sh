#/bin/sh

h="$(dirname $0)"
force=false
: ${bd:=build}
: ${PREFIX:=$HOME/.local}
eval set -- $(getopt -o b:fp: -l buildir:,force,prefix: -- "$@") || exit
while :; do
	case "$1" in
	-b|--buildir) bd="$2"; shift;;
	-f|--force) force=true;;
	-p|--prefix) PREFIX="$2"; shift;;
	--) shift; break;;
	esac
	shift
done

mkdir -p "$h/$bd" || exit
cd "$h/$bd" || exit

$force && { rm -r * 2>/dev/null || rm CMakeCache.txt 2>/dev/null; }
test -f CMakeCache.txt -a -f Makefile || \
cmake \
	-DPROJECT_VERSION=9.0.0 \
	-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX:=$PREFIX} \
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:=Debug} \
	-DWITH_SYSTEMD=${WITH_SYSTEMD:=ON} \
	-DWITH_CYNARA_COMPAT=${WITH_CYNARA_COMPAT:=OFF} \
	-DDIRECT_CYNARA_COMPAT=${DIRECT_CYNARA_COMPAT:=OFF} \
	..

make -j "$@"

