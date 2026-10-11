#!/usr/bin/env bash
# Generate a private root CA and a broker certificate signed by it.
#   tools/make_certs.sh <broker-hostname-or-ip> [more SANs...]
# Output (certs/): ca.crt (embedded in the firmware), ca.key, server.crt, server.key (broker only).
# Only ca.crt is safe to share; the .key files are git-ignored.
set -euo pipefail
cd "$(dirname "$0")/.."
host="${1:?usage: tools/make_certs.sh <broker-hostname-or-ip> [more SANs...]}"
mkdir -p certs
cd certs

san="DNS:${host}"
if [[ "$host" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then san="IP:${host}"; fi
shift || true
for extra in "$@"; do
  if [[ "$extra" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then san="${san},IP:${extra}"; else san="${san},DNS:${extra}"; fi
done

if [[ ! -f ca.key ]]; then
  openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -sha256 \
    -subj "/CN=esp32-mqtt-sensor-node local CA" -keyout ca.key -out ca.crt >/dev/null 2>&1
  echo "created ca.crt + ca.key (valid 10 years)"
fi

openssl req -newkey rsa:2048 -nodes -sha256 -subj "/CN=${host}" \
  -addext "subjectAltName=${san}" -keyout server.key -out server.csr >/dev/null 2>&1
# Broker certificate: 2 years. The ESP32 checks the hostname against the SAN, so MQTT_HOST
# in secrets.h must be exactly one of the names given here.
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 730 -sha256 \
  -copy_extensions copy -out server.crt >/dev/null 2>&1
rm -f server.csr ca.srl
chmod 600 ca.key server.key
echo "created server.crt + server.key for ${san}"
openssl x509 -in server.crt -noout -subject -ext subjectAltName -enddate
