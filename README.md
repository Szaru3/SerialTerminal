<p align="center">
  <img src="data/icons/hicolor/scalable/apps/io.github.Szaru3.SerialTerminal.svg" alt="Serial Terminal icon" width="128" height="128">
</p>

<h1 align="center">Serial Terminal</h1>

<p align="center">
  A lightweight Linux desktop application for communicating over serial ports (UART, USB-to-serial adapters, microcontrollers, and embedded hardware).
</p>

## Features

- Automatic detection of Linux serial ports (`/sys/class/tty`), plus custom port paths (e.g. `/dev/pts/2`)
- Standard baud rates from `300` to `921600` (default `115200`)
- Remembers your last used port and baud rate automatically
- Clear in-app permission hints if your user needs serial group access
- Save received output to a file, or type `/clear` to clear the terminal view
- Automatic English and Polish UI localization

## Choose Your Frontend

Serial Terminal comes with two native Linux frontends — pick the one that matches your desktop environment (or install both):

| Frontend | Desktop / Toolkit | Binary | Settings File |
| :--- | :--- | :--- | :--- |
| **KDE / Qt** | Qt 6 + KDE Frameworks 6 (`KWindowEffects` blur) | `serial-terminal` | `~/.config/SerialTerminal/SerialTerminal.conf` |
| **GNOME / GTK** | GTK 4 + Libadwaita | `serial-terminal-gnome` | `~/.config/serial-terminal/settings.ini` |

---

## Option 1: KDE / Qt 6 Frontend (`serial-terminal`)

### 1. Install dependencies

**Arch Linux / CachyOS:**
```bash
sudo pacman -S git cmake ninja qt6-base kwindowsystem
```

**Fedora:**
```bash
sudo dnf install git cmake ninja-build gcc-c++ qt6-qtbase-devel kf6-kwindowsystem-devel
```

**Debian / Ubuntu:**
```bash
sudo apt install git cmake ninja-build g++ qt6-base-dev libkf6windowsystem-dev
```

### 2. Clone and build

```bash
git clone https://github.com/Szaru3/SerialTerminal.git
cd SerialTerminal
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GNOME=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
```

### 3. Install

Install for your current user (no `sudo` required; adds launcher entry and icon):
```bash
cmake --install build
update-desktop-database "$HOME/.local/share/applications"
```

Or install system-wide (re-run `cmake` without `-DCMAKE_INSTALL_PREFIX` to default to `/usr/local`, then):
```bash
sudo cmake --install build
```

---

## Option 2: GNOME / GTK 4 Frontend (`serial-terminal-gnome`)

### 1. Install dependencies

**Arch Linux / CachyOS:**
```bash
sudo pacman -S git cmake ninja pkgconf gtk4 libadwaita
```

**Fedora:**
```bash
sudo dnf install git cmake ninja-build gcc-c++ pkgconf-pkg-config gtk4-devel libadwaita-devel
```

**Debian / Ubuntu:**
```bash
sudo apt install git cmake ninja-build g++ pkg-config libgtk-4-dev libadwaita-1-dev
```

### 2. Clone and build

```bash
git clone https://github.com/Szaru3/SerialTerminal.git
cd SerialTerminal
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
```

### 3. Install

Install for your current user (no `sudo` required; adds launcher entry and icon):
```bash
cmake --install build
update-desktop-database "$HOME/.local/share/applications"
```

Or install system-wide (re-run `cmake` without `-DCMAKE_INSTALL_PREFIX` to default to `/usr/local`, then):
```bash
sudo cmake --install build
```

---

## Option 3: Build Both Frontends

If you have the dependencies for both Qt 6 and GTK 4 installed, omit the `-DBUILD_*=OFF` flags:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
update-desktop-database "$HOME/.local/share/applications"
```

For distro packaging (`DESTDIR` staging):
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR=/path/to/pkgdir cmake --install build
```

---

## Serial Port Permissions

On Linux, opening `/dev/ttyUSB*` or `/dev/ttyACM*` usually requires membership in the serial group:

- **Arch Linux / CachyOS**:
  ```bash
  sudo usermod -aG uucp "$USER"
  ```
- **Debian / Ubuntu / Fedora**:
  ```bash
  sudo usermod -aG dialout "$USER"
  ```

Log out and log back in for the new group membership to take effect.

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
