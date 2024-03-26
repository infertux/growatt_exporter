#!/bin/bash

set -euxo pipefail

cd "$(dirname "$0")"

channel="${1:-stable}" # can be overriden with "bullseye" for example
target=growatt
container=${target}-builder-${channel}
volume=/root/HOST
cc=clang-13

if [ -z "${FAST:-}" ]; then
    docker pull "debian:${channel}"

    [ "$(docker ps -qaf "name=${container}")" ] || docker run --name "$container" --detach --tty --volume "${PWD}:${volume}" --network host "debian:${channel}"

    docker start "$container"

    docker exec "$container" dpkg --configure -a
    docker exec "$container" apt-get update
    docker exec "$container" apt-get upgrade -y
    docker exec "$container" apt-get install -y $cc
    docker exec "$container" apt-get install -y make pkg-config
    docker exec "$container" apt-get install -y libbsd-dev libconfig-dev libmodbus-dev libmosquitto-dev
    docker exec "$container" apt-get autoremove -y --purge
fi

docker exec --workdir "${volume}" "$container" rm -fv $target
docker exec --workdir "${volume}" "$container" env CC=$cc make $target
docker exec --workdir "${volume}" "$container" ls -l $target
