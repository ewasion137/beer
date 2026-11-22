# 🍻 beer

iBeer from 2008, rebuilt in pure C for your terminal. Nothing else but BEER.

https://github.com/user-attachments/assets/558a3e1a-f152-4183-a2df-3bb4488bd9d9

Pour a pint, throw your terminal window around and watch it splash. Works on Windows, macOS, BSD and Linux, also with tiling WMs.

## What's inside

- **Wave physics:** it can splash, and (not) 'realistic' physics
- **Window & WM tracking:**
  - **Windows:** Non-blocking async ConPTY writer thread keeps physics running while dragging.
  - **Linux:** Native UNIX socket IPC for Hyprland (`.socket.sock`), Sway and i3 (`i3-ipc`), `XTWINOPS` fallback on X11 and SGR mouse tracking everywhere else.
- **Smooth rendering:** 8-level sub-pixel blocks, 24-bit TrueColor gradients and diff-based frame updates to eliminate flicker at 144Hz+. (BETTER TO RUN IN A TERMINAL USING CUSTOM FONT)

## Drinks

Switch anytime with `1` - `4` or `Tab`:

| Key | Drink | Profile |
| --- | --- | --- |
| `1` | **Irish Beer** | Dark stout, thick creamy head, heavy slow waves, fine cascade bubbles |
| `2` | **German Beer** | Cloudy wheat, massive white foam, bouncy slosh |
| `3` | **Czech Beer** | Bright gold pilsner, crisp fast waves, heavy splashes |
| `4` | **Kvass** | Dark rye, coarse fizzy head that settles fast, tons of bubbles |

## Controls

| Input | Action |
| --- | --- |
| `Space` | Drink |
| `R` | Refill |
| `1` - `4` / `Tab` | Switch drink |
| `RMB/LMB + Drag` | Move window (shakes if tiled or fullscreen) |
| `Arrow keys` | Push liquid |
| `Q` / `Esc` | Quit |

## Install

### Windows

**WinGet:**
```powershell
winget install ewasion137.beer
```

**Scoop:**
```powershell
scoop bucket add ewasion137 https://github.com/ewasion137/beer
scoop install ewasion137/beer
```

Or grab `beer-windows-x64.exe` from [Releases](https://github.com/ewasion137/beer/releases).

### macOS & Linux

**Homebrew:**
```bash
brew install ewasion137/tap/beer
```

**Debian / Ubuntu:**
Download the `.deb` package from [Releases](https://github.com/ewasion137/beer/releases) and install it:
```bash
sudo apt install ./beer_*_amd64.deb
```

### Build from source

Requires `gcc` (or `clang`) and `make` (MinGW-w64 on Windows).

```bash
git clone https://github.com/ewasion137/beer
cd beer
make
```

Install system-wide on POSIX (Linux, macOS and BSD):
```bash
sudo make install
```

Run:
```bash
beer
```
