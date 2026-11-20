<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Getting started: Linux client, Windows client, Linux server

This fork (`feat/destiny-controls`) is OpenETQW with Destiny-style controls, gamepad and
Steam Deck support, and offline-only play. It runs Enemy Territory: QUAKE Wars from
**your own retail copy patched to 1.5**. Nothing here contains game data.

**Everyone needs the OpenETQW client.** OpenETQW uses its own network protocol: retail
clients can't join an OpenETQW server, and OpenETQW clients can't join retail servers.
There are no accounts, logins or master servers: the browser asks servers directly.

## 1. Downloads

Every push to this branch builds the downloads on GitHub Actions
(`.github/workflows/builds.yml`). Open
[the Builds workflow](https://github.com/athendrix/openETQW/actions/workflows/builds.yml),
pick the newest green run, and download from **Artifacts** at the bottom of its page
(you need to be signed in to GitHub; artifacts are kept 30 days):

| Artifact | What it is |
|---|---|
| `openetqw-linux-client-appimage` | the Linux client: `openetqw-<version>-x86_64.AppImage` |
| `openetqw-windows-client` | the Windows client: a zip and an installer (`...-setup.exe`) |
| `openetqw-linux-x64-package` | the Linux client and dedicated server, unpacked form (`.tar.xz`) |
| `openetqw-server-image` | the dedicated server's container image as a file (`docker load`) |

GitHub wraps each artifact in a zip; unzip it first. `<version>` is `0.1.0-dev.<run>`.
The dedicated server image is also in the GitHub container registry:

```bash
docker pull ghcr.io/athendrix/openetqw-server:feat-destiny-controls
```

(If that says "denied", the package is still private: on GitHub, open the repository's
**Packages**, `openetqw-server`, **Package settings**, and change its visibility to
public. Or use the `openetqw-server-image` artifact.)

## 2. The game data (everyone)

Every client and the server need the retail 1.5 data: a folder holding `base/` with
`pak000.pk4` to `pak008.pk4`, plus the language paks (`zpak_english000.pk4` to `003`) for
the dialogue. Copy it from an installed copy (Steam, the DVD patched to 1.5, ...). On a
server only the `base/*.pk4` files and `base/megatextures/` matter.

The client finds the data in this order:

1. `+set fs_basepath <folder>` on the command line;
2. the `OPENETQW_BASEPATH` environment variable;
3. the folder you picked the last time it asked;
4. the usual install folders, Steam libraries and Wine prefixes;
5. otherwise it asks you for the folder on its first start.

## 3. Linux client

**The AppImage** needs no install. It bundles SDL3 and takes Vulkan and OpenGL from the
system.

```bash
chmod +x openetqw-*-x86_64.AppImage
```

Either put the AppImage in the game folder (next to `base/`), where it finds the data
itself, or point it at the folder:

```bash
OPENETQW_BASEPATH="$HOME/etqw" ./openetqw-*-x86_64.AppImage
```

The `.tar.xz` package is the same program unpacked: extract it and run
`./openETQW-client_x64` the same way.

**Steam Deck:** in desktop mode, add the AppImage to Steam as a non-Steam game, with the
launch options `OPENETQW_BASEPATH=$HOME/etqw %command%` (or the folder you copied
`base/` to). The first start picks 1280x800 fullscreen, 60 fps and settings for the
Deck. For the controller, the Steam Input "Gamepad" template works, or turn Steam
Input off for the game. Details are in `docs/gamepad-and-steam-deck.md`.

Settings, bindings and the shader cache go to `~/.local/share/openetqw`.

## 4. Windows client

From the `openetqw-windows-client` artifact, either:

- run `openetqw-<version>-windows-x64-setup.exe`, which installs to Program Files and
  adds a Start menu entry; or
- unzip `openetqw-<version>-windows-x64.zip` anywhere and run
  `openETQW-client_x64.exe`.

Both bring their own SDL3, zlib and the Visual C++ runtime. Windows may warn that the
program is from an unknown publisher (it is not signed): choose **More info**, **Run
anyway**.

On its first start the client looks for the game data (section 2) and, if it can't find
it, asks for the folder holding `base\`. To choose it yourself, start it with
`+set fs_basepath "C:\Games\ETQW"` (for example in a shortcut's target).

Settings and bindings go to `%LOCALAPPDATA%\openETQW`.

To build it yourself instead: Visual Studio 2026 with "Desktop development with C++",
Python 3 with `pip install meson ninja`, Git, vcpkg in `%USERPROFILE%\vcpkg` and the
Vulkan SDK; clone both repositories side by side on this branch, then in `openETQW` run
`powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -InstallDeps`.

## 5. Linux dedicated server

The server is `openETQW-ded_x64`. It never contacts a master server and accepts any
client that has the OpenETQW client and the right password. Run it in the container
(below), or straight from the `.tar.xz` package (further down).

### In a container (Docker or Podman)

The image is Fedora Minimal with the server in `/opt/openetqw`. It keeps everything in
one volume, `/data`:

| Path in the volume | What |
|---|---|
| `/data/base/` | the retail game data: copy `pak000.pk4` ... `pak008.pk4` (and `megatextures/`) here |
| `/data/save/base/server.cfg` | the server's settings; the first start writes a starting one |
| `/data/save/` | what the server writes: its identity key (`serverkey.dat`), DHT nodes (`dht.dat`), logs |

**Set up the folder** (the container runs as user 1000, so the folder must be its):

```bash
mkdir -p ~/etqw-server/base
cp /path/to/ETQW/base/*.pk4 ~/etqw-server/base/
cp -r /path/to/ETQW/base/megatextures ~/etqw-server/base/
sudo chown -R 1000:1000 ~/etqw-server
```

**Start it:**

```bash
docker run -d --name etqw-server --restart unless-stopped -it --network host -v ~/etqw-server:/data ghcr.io/athendrix/openetqw-server:feat-destiny-controls
```

- `--network host` lets LAN players' broadcasts reach the server and gives it IPv6.
  With Docker's default network use `-p 27733:27733/udp` instead; LAN players then find
  it through the browser's slower LAN sweep, and only over IPv4.
- On Fedora (SELinux), write `-v ~/etqw-server:/data:Z`.
- With rootless Podman, use `podman` in place of `docker` and add
  `--userns=keep-id:uid=1000,gid=1000`; then the folder can stay yours (skip the
  `chown`).
- `-e ETQW_MAP=refinery` picks the first map (default `valley`), `-e ETQW_PORT=27734`
  another port. Anything after the image name goes to the server, for example
  `+set si_maxPlayers 16`.

The first start writes `~/etqw-server/save/base/server.cfg` and the server starts with it.
Edit it, then `docker restart etqw-server`. The server's console is
`docker attach etqw-server` (detach with Ctrl+P, Ctrl+Q); its output is
`docker logs etqw-server`.

From the image file instead of the registry: `gunzip -c openetqw-server-image.tar.gz |
docker load`, and use the image name it prints.

### From the package

```bash
mkdir -p ~/openetqw-server
tar -xJf openetqw-*-linux-x64.tar.xz -C ~/openetqw-server --strip-components=1
cd ~/openetqw-server
./openETQW-ded_x64 +set dedicated 1 +set fs_basepath "$HOME/etqw" +set fs_game base \
  +set net_port 27733 +exec server.cfg +spawnServer valley
```

`fs_basepath` is the folder holding `base/`. Its save path, where `server.cfg` goes
(`base/server.cfg`), is `~/.local/share/openetqw`. Type commands in the terminal
(`status`, `kick <n>`, `spawnServer <map>`, `quit`); `tmux`/`screen` or a systemd service
keep it running after you log out.

### The settings

```
set si_name "My ETQW Server"
set si_maxPlayers 24
set si_rules sdGameRulesCampaign      // or sdGameRulesObjective, sdGameRulesStopWatch
set si_pure 0
set si_teamDamage 1

// a private server: players need this password
set si_needPass 1
set g_password "changeme"
```

Anyone can use any player name, so the join password is what keeps a private server
private. Remote console (`rcon`) does not work yet: use the server's own console.

**The network:** open **UDP 27733** (or your `net_port`) in the firewall, for IPv4 and
IPv6, and for players outside your LAN forward it on your router to the server. The same
port answers the server browser and the DHT.

**Being found:** players on your LAN find the server by themselves. To be listed in
everyone's Internet tab, turn on the DHT announce and say where players reach it:

```
set net_serverAnnounce 1
set net_serverPublicAddress "203.0.113.7"
```

(a host name works too, and `:port` when it is not `net_port`; several separated by
spaces). The server then announces itself in the BitTorrent DHT every 15 minutes; the
first start asks the public DHT routers for nodes, later starts remember them.
`dhtStatus` shows the DHT node's state. Without the announce, players add the server
once with `addServer` (section 6).

**Its identity:** the server makes a key on its first start and keeps it in
`serverkey.dat` (in `/data/save` in the container). Keep that file when you move the
server: players who starred it find it again by that key on the LAN, at its public
address, and after its address changes.

## 6. Joining

**From the menu:** open the server browser. The first time in a session it opens on
your Favorites if you have saved any, else on the LAN if a server there has answered,
else on the Internet.

- **LAN** finds servers on your local network by itself (and also lists your saved
  servers). It broadcasts to UDP 27733 and, because some networks (guest Wi-Fi, some
  VPNs) drop broadcasts, also asks each address of your private subnets in turn, slowly
  (50 a second, starting when the game starts), so a server found that way shows up a
  little later. `net_lanSweepRate 0` turns the sweep off; `net_lanScanAtStart 0` waits
  for the LAN tab.
- **Internet** lists the servers that announce themselves in the BitTorrent DHT (there
  is no master server; the DHT is the public network BitTorrent clients use to find each
  other, and the game looks up one key in it), the ones found before (remembered for a
  month after they last answered, listed only while they answer), and your server list.
  A server that does not announce can be added once, in the console (the key above Tab):

  ```
  addServer your.server.address
  ```

  `addServer host:port` for a server not on port 27733, and `addServer [2001:db8::7]` or
  `addServer [2001:db8::7]:27734` for an IPv6 address; `removeServer` and `serverList`
  edit and show the list. It is saved with your settings.
- **History** lists the last ten servers you joined, and **Favorites** the ones you
  starred. Both are saved too. A starred server is remembered by its identity with all
  the addresses it has been seen at, so it is found on the LAN and over the Internet,
  and again after its address changes (when it lists its new address, or you see it at
  the new address in another tab).

Pick a server and join. For a server with a password, set it in the console first
(`set password changeme`); the retail menu's password prompt has not been tried yet.

**From the console:** `connect your.server.address` joins directly (`set password ...`
first for a passworded server), and `reconnect` joins the last server again.
`queryServers` prints what your server list answers, which is a quick way for a server
operator to check that the server can be reached (`queryServers lan` adds the LAN).

The browser asks each server directly, on the server's game port (UDP 27733); a server
answers no more than once every 100 ms per address and 60 times a second in all
(`net_serverAnswerQueries 0` turns the answers off).

## 7. Controls

- **Keyboard and mouse:** Destiny 2's PC keys over retail's (Q grenade, E interact,
  C melee, Ctrl crouch, V tools, F items, hold right mouse to aim).
- **Gamepad:** Destiny 2's controller layout. Plug in any Xbox, PlayStation or Switch Pro
  pad, or use the Steam Deck's controls.

The full layouts and every setting are in `docs/gamepad-and-steam-deck.md`. Useful console
commands:

- `in_aim_invertPitch 1` inverts the stick's look, and `m_pitch -0.022` the mouse's;
- `bindGamepadDefaults` and `bindDestinyKeyboard` restore the default layouts;
- `listControllers` shows the connected pads.
