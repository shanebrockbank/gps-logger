#!/bin/bash
PORT=${1:-/dev/ttyUSB0}
docker run --rm -v "$PWD:/project" -w /project --device="$PORT" espressif/idf idf.py -p "$PORT" flash
