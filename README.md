# spicex

Implementation of the virt-viewer feature of USB device redirections as a
daemon. This project aims to provide an interface for CLI tools to manage USB
devices redirection (list, attach and detach devices).

## Build

### Install dependencies

#### Debian

```
apt-get install -y libspice-client-gtk-3.0-dev
```

### Compiling

```
aclocal
autoconf
automake --add-missing
mkdir build && build
../configure --prefix=/usr/local
make && make install
```

## Usage

### Server

The program only takes as argument the spice port bound by the virtual machine.

```
./spicex 5900
```

It will create a Unix socket bound to `XDG_RUNTIME_DIR/spicex-5900.sock`.

The following unit systemd can also be used to run it:

```
[Unit]
Description=Spicex daemon for spice port %i

[Service]
Type=simple
ExecStart=/usr/local/bin/spicex %i
```

### Client

No client implementation is available yet, but `netcat` can be used to send commands.

#### List USB devices

```
$ echo "list:" | nc -U $XDG_RUNTIME_DIR/spicex-5900.sock
1|D-Link Elec. Corp.|D-Link DUB-1312|[2001:4a00]|2|8|1|0
2|Broadcom Corp|58200|[0a5c:5842]|1|6|1|0
3|CN0Y9V728LG003AGBCJZA01|Integrated_Webcam_FHD|[1bcf:28d2]|1|2|1|0
4|Logitech|USB Optical Mouse|[046d:c077]|1|7|1|0
```

#### Attach / detach a device

```
$ echo "attach:1" | nc -U $XDG_RUNTIME_DIR/spicex-5900.sock
success
$ echo "detach:1" | nc -U $XDG_RUNTIME_DIR/spicex-5900.sock
success
```

## Known issues

- It's possible to attach a device already attached in another virtual machine, it is the same behaviour as the virt-viewer's feature.
- It's not possible to know whether a device is already attached.
