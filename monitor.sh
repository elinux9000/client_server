#!/bin/bash
#set +e
#set -x
#These are the files to be monitored
FILE="*.h *.c monitor.sh"

function finish 
{
	echo
	echo "Restoring terminal settings"
	stty icanon echo
}

#-------	Begin main script	-------#
echo "This script runs the makefile whenever the source files are changed."

command -v inotifywait >/dev/null 2>&1 || { echo >&2 "I require inotifywait but it's not installed. You may want to try apt-get install inotify-tools."; exit 1; }

trap finish EXIT
stty -icanon -echo	#disable buffering

while [ 1 ]
do
	sleep 0.1
	cscope -b 
	make check 
	if [ $? -eq 0 ];then
		make clean
		make 
		if [ $? -eq 0 ];then
			clear
			echo "compilation OK"
			./rcserver -d &
			pid=$!
			sleep 10
			kill $pid

		fi
	fi
	modified="$(inotifywait -e modify -e move_self $FILE)"
	if [ $? -ne 0 ];then
		exit
	fi
	clear
	echo $modified
done
