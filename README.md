## btd700ctl
### unofficial linux driver for the **Sennheiser BTD 700** dongle

This project provides an interface for the missing dongle control functionality for Linux, equivalent to Sennheiser Dongle Control software.

## features
- audio mode control (high quality / low latency / broadcast)
- codec selection
- bluetooth connection mode control
- device features and status queries
- event monitoring (connection, codec changes, etc.)
- force (dis)connection of known devices
- factory reset
- [PipeWire sink switching daemon](daemon/) with optional auto-connect
- `btd700ctl` command line tool
- GNOME Quick Settings toggle

### missing
- firmware updates
- BTD 600 support (it lacks the BTD 700 control features; its desktop app only provides firmware updates)

### known bugs
- "Is gaming mode available" query doesn't work, but setting gaming mode works fine

## getting started

### requirements

- C99 compiler
- cmake
- hidapi-hidraw

```bash
# debian/ubuntu
sudo apt install build-essential cmake libhidapi-dev

# arch
sudo pacman -S base-devel cmake hidapi

# fedora
sudo dnf install gcc cmake hidapi-devel
```

### build

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```

### udev rules for non-root access

```bash
sudo cp udev/99-btd700.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## command line

```bash
btd700ctl status       # absent | disconnected | connected | streaming-audio | ...
btd700ctl info         # human readable summary (state, codec, audio mode)
btd700ctl connect      # pull the known headset over to the dongle
btd700ctl disconnect   # release the headset
btd700ctl toggle
btd700ctl monitor      # print the state now and on every change
```

## GNOME Quick Settings toggle

The BTD 700 runs its own Bluetooth stack: Linux only sees a USB sound card plus a HID control
interface, so it can never show up in GNOME's/Ubuntu's Bluetooth menu. The bundled GNOME Shell
extension (GNOME 45+) adds a **BTD 700** toggle to the Quick Settings panel instead. It shows the
current state and connects/disconnects the headset on click. It needs `btd700ctl` in `PATH`
(installed by `make install`) and the udev rules above.

```bash
cp -r gnome-extension/btd700@btd700ctl ~/.local/share/gnome-shell/extensions/
# log out and back in (required on Wayland), then:
gnome-extensions enable btd700@btd700ctl
```

## daemon

`btd700d` automatically switches your default PipeWire audio sink to the BTD700 when a bluetooth device connects, and restores the previous sink on disconnect.

```bash
# run manually
btd700d

# or install as a systemd user service
systemctl --user enable --now btd700d.service
```

With `--auto-connect` the daemon also asks the dongle to connect the headset whenever the dongle
appears (daemon start, plugging it in, docking) and the headset isn't already connected:

```bash
systemctl --user edit btd700d.service
# add:
#   [Service]
#   ExecStart=
#   ExecStart=/usr/local/bin/btd700d --auto-connect
systemctl --user restart btd700d.service
```

## contributing

Pull requests welcome! Don't hesitate to contact me if you need any help with the implementation.

## thanks to
- dnSpy/ILSpy contributors
- all the good people of the world

## license

**btd700ctl** is distributed under the GNU LGPL-2.1 License. See the [LICENSE](LICENSE) file for details.
