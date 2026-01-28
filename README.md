# SSH Helper KRunner Plugin

KRunner runner + KCM for browsing and launching SSH entries.

## Requirements

- CMake >= 3.24
- Qt 6 (Core, Gui, Widgets, Network)
- KDE Frameworks 6 (CoreAddons, I18n, Runner, Config, KCMUtils)
- Extra CMake Modules (ECM)
- `ssh` client in PATH

## Install dependencies

### Arch Linux

```bash
sudo pacman -S --needed cmake extra-cmake-modules qt6-base kcoreaddons ki18n krunner kconfig kcmutils
```

### Debian

```bash
sudo apt install cmake extra-cmake-modules qt6-base-dev libkf6coreaddons-dev libkf6i18n-dev libkf6runner-dev libkf6config-dev libkf6kcmutils-dev
```

### Ubuntu

```bash
sudo apt install cmake extra-cmake-modules qt6-base-dev libkf6coreaddons-dev libkf6i18n-dev libkf6runner-dev libkf6config-dev libkf6kcmutils-dev
```

If your distro release does not ship KDE Frameworks 6 packages yet, use a newer release or the KDE backports for your distro.

## Install

```bash
./install.sh
```

Optional overrides:

```bash
PREFIX="$HOME/.local" BUILD_DIR=./build ./install.sh
```

Restart KRunner after install (the script prints the right command for your system).

## Usage

Type `ssh` to list available targets, or `ssh <query>` to filter.
Select a result to open a terminal and connect.

## Configure

- Sources: `~/.ssh/config`, `~/.ssh/known_hosts`, plus manual entries via the KCM.
- Settings file: `~/.config/krunner_sshhelperrc`.
- Preferred terminal can be set in the KCM, or via environment:

```bash
SSH_HELPER_TERMINAL="wezterm start"
```

## Troubleshooting

- If the runner does not appear, restart KRunner and ensure the runner is enabled.
- If the KCM does not appear, ensure the install prefix is in KDE's plugin path
  (using the default `~/.local` prefix is recommended).

## Uninstall

```bash
./uninstall.sh
```

If you used a custom build directory, pass it with `BUILD_DIR=/path`.
