# Serial Terminal

A lightweight Linux desktop application for sending and receiving data over serial ports (UART, USB-to-serial adapters, microcontrollers, etc.), available with both a **Qt6 / KDE** frontend and a **GTK4 / Libadwaita (GNOME)** frontend.

## Features

- Automatic detection of available Linux serial ports (`/sys/class/tty`)
- Standard baud rates from `300` to `921600` (default `115200`)
- Save received serial output to a text file
- Native desktop look and feel:
  - `serial-terminal`: Qt6 + KWin blur (`KF6WindowSystem`)
  - `serial-terminal-gnome`: GTK4 + Libadwaita
- Automatic English and Polish UI localization

## Dependencies

- C++20 compiler (`g++` or `clang++`)
- `cmake` (>= 3.20) and `pkg-config`
- **Qt/KDE frontend (`BUILD_QT=ON`)**: `Qt6Widgets`, `KF6WindowSystem`
- **GNOME frontend (`BUILD_GNOME=ON`)**: `gtk4`, `libadwaita-1`

### Installing dependencies

**Arch Linux:**
```bash
sudo pacman -S cmake ninja pkgconf qt6-base kwindowsystem gtk4 libadwaita
```

**Fedora:**
```bash
sudo dnf install cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel kf6-kwindowsystem-devel gtk4-devel libadwaita-devel
```

**Debian / Ubuntu:**
```bash
sudo apt install cmake ninja-build pkg-config qt6-base-dev libkf6windowsystem-dev libgtk-4-dev libadwaita-1-dev
```

## Build & Install

Build both frontends:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

You can also build only one frontend:
```bash
# Qt/KDE only
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GNOME=OFF

# GTK4/GNOME only
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT=OFF
```

Install system-wide (defaults to `/usr/local`; pass `-DCMAKE_INSTALL_PREFIX=/usr` for packaging):
```bash
sudo cmake --install build
```

For distro packaging (`DESTDIR` staging):
```bash
DESTDIR=/path/to/pkgdir cmake --install build
```

## Serial Port Permissions

On Linux, opening `/dev/ttyUSB*` or `/dev/ttyACM*` typically requires membership in the serial group:

- **Debian / Ubuntu / Fedora**:
  ```bash
  sudo usermod -aG dialout "$USER"
  ```
- **Arch Linux**:
  ```bash
  sudo usermod -aG uucp "$USER"
  ```

Log out and log back in after adding yourself to the group.

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
