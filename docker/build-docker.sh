#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

DOCKER_IMAGE=${DOCKER_IMAGE:-trivechainpay/trivechaind-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/trivechaind docker/bin/
cp $BUILD_DIR/src/trivechain-cli docker/bin/
cp $BUILD_DIR/src/trivechain-tx docker/bin/
strip docker/bin/trivechaind
strip docker/bin/trivechain-cli
strip docker/bin/trivechain-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
