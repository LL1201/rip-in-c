#!/bin/bash

# Usciamo immediatamente se un comando fallisce
set -e

# Verifica se lo script è eseguito come root
if [ "$EUID" -ne 0 ]; then
  echo "Questo script deve essere eseguito con i privilegi di root"
  exit 1
fi

echo "Avviode i container con Docker Compose:"
docker compose up -d --build

# per attendere l'inizializzazione dei container
sleep 3

PID_A=$(docker inspect -f '{{.State.Pid}}' router-A)
PID_B=$(docker inspect -f '{{.State.Pid}}' router-B)
PID_C=$(docker inspect -f '{{.State.Pid}}' router-C)

# ==========================================
# 1. LINK ROUTER A <--> ROUTER B (Rete 10.12.1.0/24)
# ==========================================
ip link add veth-ab type veth peer name veth-ba

# Sposta nei namespace
ip link set veth-ab netns $PID_A
ip link set veth-ba netns $PID_B

# Configura lato Router A
docker exec router-A ip link set veth-ab name eth-ab
docker exec router-A ip link set eth-ab up
docker exec router-A ip addr add 10.12.1.3/24 dev eth-ab

# Configura lato Router B
docker exec router-B ip link set veth-ba name eth-ba
docker exec router-B ip link set eth-ba up
docker exec router-B ip addr add 10.12.1.2/24 dev eth-ba


# ==========================================
# 2. LINK ROUTER A <--> ROUTER C (Rete 10.12.4.0/24)
# ==========================================
ip link add veth-ac type veth peer name veth-ca

# Sposta nei namespace
ip link set veth-ac netns $PID_A
ip link set veth-ca netns $PID_C

# Configura lato Router A
docker exec router-A ip link set veth-ac name eth-ac
docker exec router-A ip link set eth-ac up
docker exec router-A ip addr add 10.12.4.2/24 dev eth-ac

# Configura lato Router C
docker exec router-C ip link set veth-ca name eth-ca
docker exec router-C ip link set eth-ca up
docker exec router-C ip addr add 10.12.4.3/24 dev eth-ca


# ==========================================
# 3. LINK ROUTER A <--> ROUTER C [2] (Rete 10.12.5.0/24)
# ==========================================
ip link add veth-ac2 type veth peer name veth-c2a

# Sposta nei namespace
ip link set veth-ac2 netns $PID_A
ip link set veth-c2a netns $PID_C

# Configura lato Router A
docker exec router-A ip link set veth-ac2 name eth-ac2
docker exec router-A ip link set eth-ac2 up
docker exec router-A ip addr add 10.12.5.2/24 dev eth-ac2

# Configura lato Router C
docker exec router-C ip link set veth-c2a name eth-ca2
docker exec router-C ip link set eth-ca2 up
docker exec router-C ip addr add 10.12.5.3/24 dev eth-ca2

#avvio dei processi rip nei container

# Reindirizziamo l'output (stdout e stderr) sul PID 1 del container in modo da avere i log su docker logs
docker exec -d router-A bash -c "/app/rip-in-c > /proc/1/fd/1 2>&1"
docker exec -d router-B bash -c "/app/rip-in-c > /proc/1/fd/1 2>&1"
docker exec -d router-C bash -c "/app/rip-in-c > /proc/1/fd/1 2>&1"

echo "--------------------------------------------------------"
echo "Laboratorio avviato"