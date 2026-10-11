#!/usr/bin/env bash
# Create the Mosquitto password file for docker-compose (needs Docker, no local mosquitto install).
#   tools/broker_setup.sh <username> <passphrase>
# Writes docker/passwd (git-ignored). Run tools/make_certs.sh first, then `docker compose up -d`.
set -euo pipefail
cd "$(dirname "$0")/.."
user="${1:?usage: tools/broker_setup.sh <username> <passphrase>}"
pass="${2:?usage: tools/broker_setup.sh <username> <passphrase>}"
[[ -f certs/server.crt ]] || { echo "run tools/make_certs.sh first"; exit 1; }
touch docker/passwd
docker run --rm -v "$PWD/docker/passwd:/passwd" eclipse-mosquitto:2 \
  mosquitto_passwd -b /passwd "$user" "$pass"
chmod 600 docker/passwd
echo "docker/passwd written for user '$user'. Put the same values in include/secrets.h and .env"
