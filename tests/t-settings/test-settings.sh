#!/bin/bash

tmp=$(mktemp)

a() {
	echo "tmpfile: $tmp"
	echo "$*" | tee "$tmp"
	test-settings "$tmp"
}

q() {
	local front="$1" cmd="${2:-set}" val
	case $# in
	0) ;;
	1) shift 1;;
	*) shift 2;;
	esac
	case $cmd in
	set)
		case $# in
		0)
			val="$(printf "%s\n  #### END ####\n" "$front")"
			a "${val}"
			;;
		1)
			val="$(printf "%s# <%s>\n\n%s\t#%s\n\n" "$front" "$1" "$1" "$1")"
			q "${val}" set
			;;
		*)
			val="$(printf "%s# <%s> <%s>\n\n%s\t%s\t#%s %s\n\n" "$front" "$1" "$2" "$1" "$2" "$1" "$2")"
			shift 2
			q "${val}" "$@"
		   	;;
		esac
		;;
	bool)
		val="$1"
		shift
		q "${front}" set "$val" "yes" "$@"
		q "${front}" set "  $val" "no" "$@"
		q "${front}" set "$val" "z" "$@"
		q "${front}" set "$val" "" "$@"
		;;
	str)
		val="$1"
		shift
		q "${front}" set "  $val" "xxx" "$@"
		q "${front}" set "$val" "" "$@"
		;;
	esac
}

b() {
	q "" "$@"
}

b set 1
b set truc
b str dbdir
b str socketdir
b str init
b str user
b str group
b bool make-socket-dir
b bool make-db-dir
b bool own-db-dir
b bool own-socket-dir

if false; then
b str dbdir \
  str socketdir \
  str init \
  str user \
  str group \
  bool make-socket-dir \
  bool make-db-dir \
  bool own-db-dir \
  bool own-socket-dir
fi

rm "$tmp"
