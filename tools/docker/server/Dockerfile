# SPDX-License-Identifier: GPL-3.0-or-later
#
# The openETQW dedicated server on Fedora Minimal (.github/workflows/builds.yml builds
# and pushes it; GETTING-STARTED.md section 4 runs it). The build context holds the
# unpacked Linux package as server/ and this folder's files.
#
# /data is the one volume:
#   /data/base   the retail game data (pak000.pk4 ...), which this image does not include
#   /data/save   what the server writes: server.cfg (base/server.cfg), its identity key
#                (serverkey.dat), the DHT's nodes (dht.dat), logs
FROM registry.fedoraproject.org/fedora-minimal:45

RUN microdnf install -y --nodocs --setopt=install_weak_deps=0 libstdc++ zlib \
    && microdnf clean all

COPY server/ /opt/openetqw/
COPY entrypoint.sh /opt/openetqw/entrypoint.sh
COPY server.cfg.example /opt/openetqw/server.cfg.example
RUN chmod 0755 /opt/openetqw/entrypoint.sh \
    && mkdir -p /data \
    && chown 1000:1000 /data

# not root; the volume's files belong to uid 1000
USER 1000:1000
ENV HOME=/data/save \
    ETQW_CONFIG=server.cfg \
    ETQW_MAP=valley \
    ETQW_PORT=27733
VOLUME ["/data"]
EXPOSE 27733/udp
WORKDIR /opt/openetqw
ENTRYPOINT ["/opt/openetqw/entrypoint.sh"]
