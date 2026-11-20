<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Getting started: Linux client, Windows client, Linux server

This fork (`feat/destiny-controls`) is OpenETQW with Destiny-style controls, gamepad and
Steam Deck support, and offline-only play. It runs Enemy Territory: QUAKE Wars from
**your own retail copy patched to 1.5**. Nothing here contains game data.

**Everyone needs the OpenETQW client.** OpenETQW uses its own network protocol: retail
clients can't join an OpenETQW server, and OpenETQW clients can't join retail servers.
There are no accounts, logins or master servers: the browser asks servers directly.

## 1. The game data (everyone)

Every client and the server need the retail 1.5 data: a folder holding `base/` with
`pak000.pk4` to `pak008.pk4`, plus the language paks (`zpak_english000.pk4` to `003`) for
the dialogue. Copy it from an installed copy (Steam, the DVD patched to 1.5, ...). On a
server only the `base/*.pk4` files and `base/megatextures/` matter.

The engine finds the data in this order:

1. `+set fs_basepath <folder>` on the command line;
2. the `OPENETQW_BASEPATH` environment variable;
3. the folder you picked the last time it asked;
4. the usual install folders, Steam libraries and Wine prefixes;
5. otherwise the client asks you for the folder on its first start.

## 2. Linux client

**The AppImage** (`openetqw-<version>-x86_64.AppImage`) needs no install. It bundles
SDL3 and takes Vulkan and OpenGL from the system.

```bash
chmod +x openetqw-0.1.0-destiny-x86_64.AppImage
```

Either put the AppImage in the game folder (next to `base/`), where it finds the data
itself, or point it at the folder:

```bash
OPENETQW_BASEPATH="$HOME/etqw" ./openetqw-0.1.0-destiny-x86_64.AppImage
```

The `.tar.xz` package is the same program unpacked: extract it and run
`./openETQW-client_x64` the same way.

**Steam Deck:** in desktop mode, add the AppImage to Steam as a non-Steam game, with the
launch options `OPENETQW_BASEPATH=$HOME/etqw %command%` (or the folder you copied
`base/` to). The first start picks 1280x800 fullscreen, 60 fps and settings for the
Deck. For the controller, the Steam Input "Gamepad" template works, or turn Steam
Input off for the game. Details are in `docs/gamepad-and-steam-deck.md`.

Settings, bindings and the shader cache go to `~/.local/share/openetqw`.

## 3. Windows client

There is no Windows download yet: build it from source on the Windows PC. These steps
have not been tried on Windows with this branch yet. The upstream project builds and
tests Windows, but this branch's changes have only been compiled on Linux, so report
any build error.

**Install first:**

- Visual Studio 2026 (Community is fine) with "Desktop development with C++";
- Python 3, then `python -m pip install --user meson ninja`;
- Git;
- [vcpkg](https://github.com/microsoft/vcpkg), cloned to `%USERPROFILE%\vcpkg` and
  bootstrapped (`bootstrap-vcpkg.bat`);
- the [Vulkan SDK](https://vulkan.lunarg.com).

**Get the code**, both repositories side by side, on this branch. The engine
expects the game repository at `..\openETQW-game`:

```powershell
git clone -b feat/destiny-controls https://github.com/athendrix/openETQW.git
git clone -b feat/destiny-controls https://github.com/athendrix/openETQW-game.git
cd openETQW
```

**Build and run.** `-InstallDeps` installs SDL3 and the other libraries through vcpkg
the first time; it needs no administrator rights.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -InstallDeps
$env:ETQW_DATA_PATH = "C:\Games\ETQW"
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Run
```

`ETQW_DATA_PATH` is the folder holding `base\` (the helper passes it on as
`fs_basepath`). The client ends up at `build\openETQW-client_x64.exe` with the game
module next to it, and later you can start that directly, with
`+set fs_basepath C:\Games\ETQW`, or let it ask for the folder.

Settings and bindings go to `%LOCALAPPDATA%\openETQW`.

## 4. Linux dedicated server

The dedicated server is `openETQW-ded_x64`, in the `.tar.xz` package (not the
AppImage). It replaces the retail `etqwded` binary: it never contacts a master server,
and accepts any client that has the OpenETQW client and the right password.

**Install:**

```bash
mkdir -p ~/openetqw-server
tar -xJf openetqw-0.1.0-destiny-linux-x64.tar.xz -C ~/openetqw-server --strip-components=1
```

Copy the retail `base/` folder to the server too, for example to `~/etqw/base`.

**A server config**, `~/.local/share/openetqw/base/server.cfg` (the server's save path;
`+exec` finds it there):

```
set si_name "My ETQW Server"
set si_maxPlayers 24
set si_rules sdGameRulesCampaign      // or sdGameRulesObjective, sdGameRulesStopWatch
set si_pure 0
set si_teamDamage 1

// a private server: players need this password
set si_needPass 1
set g_password "changeme"

// remote admin from a client's console: rcon <command>
set net_serverRemoteConsolePassword "a-long-random-password"
```

Choose your own passwords; anyone can use any player name, so the join password is
what keeps a private server private.

**Start it** (a campaign, or a single map with `si_rules sdGameRulesObjective`):

```bash
cd ~/openetqw-server
./openETQW-ded_x64 +set dedicated 1 +set fs_basepath "$HOME/etqw" +set fs_game base \
  +set net_port 27733 +exec server.cfg +spawnServer valley
```

Type commands in the terminal (`status`, `kick <n>`, `spawnServer <map>`, `quit`). To
keep it running after you log out, use `tmux`/`screen` or a systemd service.

**The network:** open **UDP 27733** (or the `net_port` you chose) in the firewall and,
for players outside your LAN, forward it on your router to the server. The same port
answers the menu's server browser. Players add the server to their list once with
`addServer` (section 5).

**Being found:** to have players' Internet tab list the server, turn on its DHT
announce and tell it its public address:

```
set net_serverAnnounce 1
```

It then announces its game port in the DHT every 15 minutes (on the same UDP port, so
nothing more to open). `dhtStatus` shows the DHT node's state. The first start asks the
public DHT routers for nodes; after that the server remembers nodes in `dht.dat`.

**Its identity:** the server makes a key the first time it answers the browser and keeps
it in `serverkey.dat` in its save path (keep that file when you move the server). Players
who starred the server find it again by that key on the LAN, at its public address, and
after its address changes. Tell it the address players outside your LAN use:

```
set net_serverPublicAddress "203.0.113.7"
```

(a host name works too, and `:port` when it is not `net_port`; several separated by
spaces). The server lists it, with its global IPv6 addresses and its LAN addresses, in the
answer it signs.

**IPv6:** the game and the server use IPv6 next to IPv4 on the same port number (open
UDP 27733 for both in the firewall); a LAN search also asks every IPv6 neighbour.
`net_ipv6 0` turns IPv6 off.

## 5. Joining

**From the menu:** open the server browser.

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

## 6. Controls

- **Keyboard and mouse:** Destiny 2's PC keys over retail's (Q grenade, E interact,
  C melee, Ctrl crouch, V tools, F items, hold right mouse to aim).
- **Gamepad:** Destiny 2's controller layout. Plug in any Xbox, PlayStation or Switch Pro
  pad, or use the Steam Deck's controls.

The full layouts and every setting are in `docs/gamepad-and-steam-deck.md`. Useful console
commands:

- `in_aim_invertPitch 1` inverts the stick's look, and `m_pitch -0.022` the mouse's;
- `bindGamepadDefaults` and `bindDestinyKeyboard` restore the default layouts;
- `listControllers` shows the connected pads.
