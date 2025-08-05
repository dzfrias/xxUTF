FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    wget \
    lsb-release \
    software-properties-common \
    gnupg \
    && rm -rf /var/lib/apt/lists/*

RUN bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"

# Install AFL++
RUN git clone https://github.com/AFLplusplus/AFLplusplus.git /opt/aflplusplus && \
    cd /opt/aflplusplus && \
    make source-only && \
    make install

RUN wget https://github.com/JuliaStrings/utf8proc/releases/download/v2.10.0/utf8proc-2.10.0.tar.gz && \
    tar -xzf utf8proc-2.10.0.tar.gz && \
    rm utf8proc-2.10.0.tar.gz && \
    mv utf8proc-2.10.0 /opt/utf8proc && \
    cd /opt/utf8proc && \
    make && \
    make install && \
    ldconfig

# Add AFL++ to PATH
ENV PATH="/opt/aflplusplus:$PATH"

VOLUME /output
VOLUME /code
