#!/bin/bash

set -euxo pipefail

cd "$(dirname "$0")"

target="${1:-growatt}"
interactive="${2:-}"
container=${target}-builder
volume=/root/HOST
channel=stable

if [ -z "${FAST:-}" ]; then
    docker pull debian:${channel}

    [ "$(docker ps -qaf "name=${container}")" ] || docker run --name $container -d -t -v "${PWD}:${volume}" debian:${channel}

    docker start $container

    docker exec $container dpkg --configure -a
    docker exec $container bash -c "echo \"deb http://ftp.sg.debian.org/debian ${channel} main\" | tee /etc/apt/sources.list"
    docker exec $container apt-get update
    docker exec $container apt-get upgrade -y
    docker exec $container apt-get install -y make clang pkg-config
    docker exec $container apt-get install -y libbsd-dev libmodbus-dev
    docker exec $container apt-get autoremove -y --purge
fi

docker exec --workdir "${volume}" $container rm -fv $target
docker exec --workdir "${volume}" $container make $target
docker exec --workdir "${volume}" $container ls -l $target
