#! /bin/sh

TMPFILE=`mktemp` || exit 1
TMPFILE2=`mktemp` || exit 1

trap "rm -f $TMPFILE $TMPFILE2" EXIT ERR INT HUP QUIT

ITERS=20
prefix="$1"

set -e

echo '# n ksql sqlbox sqlite3'

for f in 500 1000 2000 4000 8000 16000
do
	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt $ITERS ]]
	do
		echo /usr/bin/time "./$prefix-ksql" -n $f 1>&2
		/usr/bin/time "./$prefix-ksql" -n $f >/dev/null 2>$TMPFILE2
		awk '{print $1}' $TMPFILE2 >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v1=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt $ITERS ]]
	do
		echo /usr/bin/time "./$prefix-sqlbox" -n $f 1>&2
		/usr/bin/time "./$prefix-sqlbox" -n $f >/dev/null 2>$TMPFILE2
		awk '{print $1}' $TMPFILE2 >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v2=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	i=0
	cat /dev/null >$TMPFILE
	while [[ $i -lt $ITERS ]]
	do
		echo /usr/bin/time "./$prefix-sqlite3" -n $f 1>&2
		/usr/bin/time "./$prefix-sqlite3" -n $f >/dev/null 2>$TMPFILE2
		awk '{print $1}' $TMPFILE2 >> $TMPFILE
		i=$(( $i + 1 ))
	done
	v3=`awk '{sum+=$1; sumsq+=$1*$1} END {print sum/NR, sqrt(sumsq/NR - (sum/NR)^2)}' $TMPFILE`

	echo $f $v1 $v2 $v3
done
