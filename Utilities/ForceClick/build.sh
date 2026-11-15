#!/bin/bash
# Builds the two command line tools next to this script (Xcode command line tools required).
set -e
cd "$(dirname "$0")"
swiftc -O vi2c-forcectl.swift -o vi2c-forcectl
swiftc -O ForceProbe.swift -o ForceProbe
echo "built: $(pwd)/vi2c-forcectl $(pwd)/ForceProbe"
