#!/bin/bash

set -x -e -u -o pipefail

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-./_build}
BUILD_TYPE=${BUILD_TYPE:-debug}
INSTALL_DIR=${INSTALL_DIR:-../${BUILD_TYPE}-install}
MUDUO_BUILD_DIR=${MUDUO_BUILD_DIR:-./}

mkdir -p ${BUILD_DIR}/${BUILD_TYPE} \
  && cd ${BUILD_DIR}/${BUILD_TYPE} \
  && cmake \
     -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
     -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
     -DMUDUO_BUILD_DIR=${MUDUO_BUILD_DIR} \
     $SOURCE_DIR \
  && make $*
