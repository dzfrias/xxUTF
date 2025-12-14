FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    pkg-config \
    git \
    wget \
    lsb-release \
    software-properties-common \
    gnupg \ 
    && rm -rf /var/lib/apt/lists/*

RUN wget https://github.com/unicode-org/icu/releases/download/release-77-1/icu4c-77_1-src.tgz \
    && tar -xzf icu4c-77_1-src.tgz \
    && cd icu/source \
    && ./configure --prefix=/usr/local \
    && make -j$(nproc) \
    && make install \
    && ldconfig \
    && cd ../.. \
    && rm -rf icu icu4c-77_1-src.tgz

# Verify installation
RUN icu-config --version

RUN bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"

# Install AFL++
RUN git clone https://github.com/AFLplusplus/AFLplusplus.git /opt/aflplusplus && \
    cd /opt/aflplusplus && \
    make source-only && \
    make install

# Add AFL++ to PATH
ENV PATH="/opt/aflplusplus:$PATH"

VOLUME /output
VOLUME /code
