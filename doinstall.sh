# 1. First of all install all the dependencies using below command
#bpftool \
sudo apt update
sudo apt install -y \
  build-essential \
  clang \
  llvm \
  gcc \
  make \
  pkg-config \
  libelf-dev \
  libbpf-dev \
  libbpf-tools \
  bpftrace \
  linux-tools-common \
  bpfcc-tools \
  linux-headers-$(uname -r) \
  git \
  python3 \
  python3-pip
