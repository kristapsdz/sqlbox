#! /bin/sh

TMPFILE=`mktemp` || exit 1

trap "rm -f $TMPFILE tmp.dat" EXIT ERR INT HUP QUIT

prefix="$1"

set -e

echo '# n ksql sqlbox sqlite3'

for f in 100 1000 10000 20000 40000
do
	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt 10 ]]
	do
		echo /usr/bin/time "./$prefix-ksql" -n $f 1>&2
		/usr/bin/time "./$prefix-ksql" -n $f >/dev/null 2>tmp.dat
		awk '{print $1}' tmp.dat >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v1=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt 10 ]]
	do
		echo /usr/bin/time "./$prefix-sqlbox" -n $f 1>&2
		/usr/bin/time "./$prefix-sqlbox" -n $f >/dev/null 2>tmp.dat
		awk '{print $1}' tmp.dat >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v2=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt 10 ]]
	do
		echo /usr/bin/time "./$prefix-sqlite3" -n $f 1>&2
		/usr/bin/time "./$prefix-sqlite3" -n $f >/dev/null 2>tmp.dat
		awk '{print $1}' tmp.dat >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v3=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	echo $f $v1 $v2 $v3
done
