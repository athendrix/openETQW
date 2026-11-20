#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts the dedicated server on the container's /data volume (Dockerfile). Extra
# arguments go to the server after the defaults, e.g. "+set si_maxPlayers 16".
set -eu

if ! ls /data/base/pak000.pk4 >/dev/null 2>&1; then
    echo "openETQW: no game data. Copy the retail base/ folder (pak000.pk4 and the rest)" >&2
    echo "to /data/base on the volume, then start the container again." >&2
    exit 1
fi

mkdir -p /data/save/base
if [ ! -f /data/save/base/server.cfg ]; then
    cp /opt/openetqw/server.cfg.example /data/save/base/server.cfg
    echo "openETQW: wrote a starting config to /data/save/base/server.cfg; edit it and restart"
fi

case "$(uname -m)" in
    aarch64) server=/opt/openetqw/openETQW-ded_arm64 ;;
    *) server=/opt/openetqw/openETQW-ded_x64 ;;
esac

exec "$server" +set dedicated 1 +set fs_basepath /data +set fs_savepath /data/save \
    +set fs_game base +set net_port "$ETQW_PORT" +exec server.cfg "$@" +spawnServer "$ETQW_MAP"
