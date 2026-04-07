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
- [PipeWire sink switching daemon](daemon/)

### missing
- software updates
- broadcast settings
- BTD 600 support (?)

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

## daemon

`btd700d` automatically switches your default PipeWire audio sink to the BTD700 when a bluetooth device connects, and restores the previous sink on disconnect.

```bash
# run manually
btd700d

# or install as a systemd user service
systemctl --user enable --now btd700d.service
```

## contributing

Pull requests welcome! Don't hesitate to contact me if you need any help with the implementation.

## thanks to
- dnSpy/ILSpy contributors
- all the good people of the world

## license

**btd700ctl** is distributed under the GNU LGPL-2.1 License. See the [LICENSE](LICENSE) file for details.
