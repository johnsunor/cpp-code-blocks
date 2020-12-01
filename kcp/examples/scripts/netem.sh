#!/bin/bash

set -x -u -o pipefail

if [ $# != 4 ]; then
  echo "Usage: bash ${0} <dev> <dest_ip> <dest_port> <loss_rate>"
  exit
fi

DEV=${1}
DEST_IP=${2}
DEST_PORT=${3}
LOSS_RATE=${4}

tc qdisc del dev ${DEV} root 2> /dev/null > /dev/null

set -e

# root
tc qdisc add dev eth0 root handle 1: htb default 10

# default class
tc class add dev eth0 parent 1: classid 1:10 htb rate 1000mbit
# test class
tc class add dev eth0 parent 1: classid 1:20 htb rate 1000mbit

# default qdisc
tc qdisc add dev eth0 parent 1:10 handle 10: fq_codel
# test qdisc
tc qdisc add dev eth0 parent 1:20 handle 20: netem delay 20ms 5ms 25% reorder 10% 25% duplicate 2% corrupt 2% loss ${LOSS_RATE}%

# filter
tc filter add dev eth0 parent 1: protocol ip prio 1 u32 match ip dst ${DEST_IP}/32 match ip dport ${DEST_PORT} 0xffff flowid 1:20
