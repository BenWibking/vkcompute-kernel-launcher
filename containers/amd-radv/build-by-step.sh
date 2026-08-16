#!/usr/bin/env bash
set -euo pipefail

docker build --platform linux/amd64 -t vkcompute-amdgpu:main .
docker tag vkcompute-amdgpu:main ghcr.io/benwibking/vkcompute-kernel-launcher:main
docker push ghcr.io/benwibking/vkcompute-kernel-launcher:main
