# 🐻 Bear Run

A C++ side-scrolling runner game built with SDL2. Play as a bear dodging obstacles and collecting coins through an endless night forest.

---

## Controls

| Key | Action |
|-----|--------|
| `SPACE` or `↑` | Jump |
| `SPACE` or `↑` (again mid-air) | Double jump |
| `↓` | Duck |
| `ESC` | Quit |

---

## Gameplay

- **Dodge** logs, cacti, rocks, and flying bees
- **Collect coins** on the ground and in the air
- **3 lives** — you flash briefly after each hit (invincibility window)
- **Speed increases** over time — how long can you survive?
- Your **best score** is tracked across runs

---

## Building

### Requirements

- `g++` with C++17 support
- SDL2
- SDL2_ttf

### Install dependencies

**Ubuntu / Debian**
```bash
sudo apt update
sudo apt install libsdl2-dev libsdl2-ttf-dev
```

**macOS**
```bash
brew install sdl2 sdl2_ttf
```

**Windows (MSYS2)**
```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf
```

### Compile

```bash
g++ bear_run.cpp -o bear_run -lSDL2 -lSDL2_ttf -std=c++17 -O2
```

Or with the Makefile:
```bash
make
```

### Run

```bash
./bear_run
```

---

## Obstacles

| Obstacle | Description |
|----------|-------------|
| 🪵 Log | Short and wide — jump over it |
| 🌵 Cactus | Tall — jump or double jump |
| 🪨 Rock | Low and wide — jump over it |
| 🐝 Bee | Flies at mid-height — duck under or jump over |

---

## Project Structure

```
bear_run.cpp   — full game source (~650 lines)
Makefile       — build helper
README.md      — this file
```

---

## Based on

Original bear physics prototype by the user, extended with obstacles, coins, lives, parallax background, animations, and a HUD.
