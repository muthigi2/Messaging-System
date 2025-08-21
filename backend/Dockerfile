# syntax=docker/dockerfile:1
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
  build-essential cmake libboost-all-dev \
  && rm -rf /var/lib/apt/lists/*

# Render "Root Directory" is set to backend, so /app contains backend sources
WORKDIR /app
COPY . .

# Build backend from current directory
RUN cmake -S . -B build \
  && cmake --build build -j

EXPOSE 8080
# Executable target is named "backend" and lives in build/
CMD ["/app/build/backend"] 