#!/bin/bash

ROOT_DIR=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
APP_PATH="${ROOT_DIR}/build/test/"

export TEST_PIE_SERVER_IP="127.0.0.1"
export TEST_PIE_CONN_NUM=4

${APP_PATH}${1}
