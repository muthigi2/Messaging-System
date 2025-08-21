# syntax=docker/dockerfile:1
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
  build-essential cmake libboost-all-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

# Build backend
RUN cmake -S backend -B build \
  && cmake --build build -j

EXPOSE 8080
CMD ["/app/build/backend/backend"] 