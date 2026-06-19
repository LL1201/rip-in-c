#compilazione
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y gcc make
WORKDIR /app
COPY src/ ./src
COPY Makefile .
RUN make

#esecuzione
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y iputils-ping iproute2 tcpdump net-tools
WORKDIR /app
COPY --from=builder /app/rip-in-c .
CMD ["./rip-in-c"]