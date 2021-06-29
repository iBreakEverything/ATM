#!/bin/bash

if [ $# -ne 4 ]
	then
		echo "Usage: $0 <IP> <PORT> <USER_DATA_FILE> <INPUT_TEST_FILE>"
		exit -1
fi

IP=$1
PORT=$2
USER_DATA_FILE=$3
INPUT_TEST_FILE=$4

PID_SERVER=$(pidof server)

if [ -n "${PID_SERVER}" ]; then
	kill -9 $PID_SERVER
fi

./server $PORT $USER_DATA_FILE &

sleep 1

./aux_testing.sh $INPUT_TEST_FILE | ./client $IP $PORT

PID_SERVER=$(pidof server)

kill -9 $PID_SERVER