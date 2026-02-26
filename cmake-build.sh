#!/bin/bash
cmake_build_root=cmake-build

if [ -z "$VCPKG_ROOT" ]; then
    echo "VCPKG_ROOT环境变量未设置"
    exit 1
fi

vcpkg_triplet=arm64-osx

rm -rf $cmake_build_root
mkdir $cmake_build_root
cmake -S . -B $cmake_build_root -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
-DVCPKG_TARGET_TRIPLET=$vcpkg_triplet \
-DVCPKG_INSTALLED_DIR=vcpkg_installed \
-DVCPKG_MANIFEST_MODE=OFF
