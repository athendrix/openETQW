#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts the dedicated server on the container's /data volume (Dockerfile).
#   ETQW_CONFIG  the config to exec (default server.cfg), from /data/save/base or
#                /data/base
#   ETQW_MAP     the map or campaign to start (default valley); empty when the config
#                starts one itself (a map rotation: "vstr m0")
#   ETQW_PORT    the UDP port (default 27733)
# Extra arguments go to the server after the config, e.g. "+set si_maxPlayers 16".
set -eu

config="${ETQW_CONFIG:-server.cfg}"
map="${ETQW_MAP-valley}"
port="${ETQW_PORT:-27733}"

if ! ls /data/base/pak000.pk4 >/dev/null 2>&1; then
    echo "openETQW: no game data. Copy the retail base/ folder (pak000.pk4 and the rest)" >&2
    echo "to /data/base on the volume, then start the container again." >&2
    exit 1
fi

mkdir -p /data/save/base
if [ ! -f "/data/save/base/$config" ] && [ ! -f "/data/base/$config" ]; then
    if [ "$config" = server.cfg ]; then
        cp /opt/openetqw/server.cfg.example /data/save/base/server.cfg
        echo "openETQW: wrote a starting config to /data/save/base/server.cfg; edit it and restart"
    else
        echo "openETQW: $config is in neither /data/save/base nor /data/base" >&2
        exit 1
    fi
fi

case "$(uname -m)" in
    aarch64) server=/opt/openetqw/openETQW-ded_arm64 ;;
    *) server=/opt/openetqw/openETQW-ded_x64 ;;
esac

if [ -n "$map" ]; then
    exec "$server" +set dedicated 1 +set fs_basepath /data +set fs_savepath /data/save \
        +set fs_game base +set net_port "$port" +exec "$config" "$@" +spawnServer "$map"
fi
exec "$server" +set dedicated 1 +set fs_basepath /data +set fs_savepath /data/save \
    +set fs_game base +set net_port "$port" +exec "$config" "$@"
