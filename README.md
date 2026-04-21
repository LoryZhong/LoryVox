# LoryVox

A 3D rotational persistence-of-vision (POV) volumetric display built on a Raspberry Pi 4, driven by two HUB75E RGB LED panels spinning on a custom 3D-printed rotor. The display synthesises a three-dimensional image by presenting different 2D slices at precise angular positions as the panels rotate, exploiting the visual system's persistence of vision to integrate them into a static 3D percept.

This is a final-year EEE project at UCL (2025).

---

## Hardware

| Component | Specification |
|-----------|--------------|
| Single-board computer | Raspberry Pi 4 (4 GB) |
| LED panels | 2× P2.5 64×64 HUB75E (ICN2037 driver) |
| Motor | RS PRO 216-3789 BLDC, 24 V, 4800 RPM rated |
| Transmission | GT2 belt and pulley |
| Slip ring | ASL9013 2-line, 24 V |
| Brush holder | AS-PL ABH6004S carbon brush |
| Synchronisation | Photo-interrupter (half-disc flag, 1 pulse/rev) |
| PCB adapter | Custom 2-layer HUB75E level-shifter (74HCT245 × 2) |
| Power | 24 V SMPS → on-rotor 24 V→5 V DC-DC converter |
| Structure | FDM 3D-printed PLA (structural) + PETG (motor damper) |

---

## Repository Layout

```
├── src
│   ├── driver
│   │   ├── gadgets         -- hardware configurations (GPIO mapping, panel layout)
│   │   └── vortex.c        -- DMA-driven display driver, shared-memory voxel buffer
│   ├── simulator
│   │   └── virtex.c        -- OpenGL software simulator (no hardware needed)
│   ├── multivox            -- launcher / front end
│   ├── platform            -- common client code
│   └── toys
│       ├── tesseract.c     -- 4D hypercube demo
│       ├── viewer.c        -- OBJ / PNG file viewer with zoom-fit
│       └── ...             -- other bundled demos
├── python
│   ├── obj2c.py            -- embed .obj models in a header file
│   └── ...
└── README.md
```

---

## Building

Clone the repository on the Raspberry Pi:

```bash
git clone https://github.com/LoryZhong/LoryVox.git
cd LoryVox
mkdir build && cd build
cmake -DMULTIVOX_GADGET=vortex ..
cmake --build .
```

---

## Running

Start the display driver (requires root for DMA/GPIO access):

```bash
sudo ./vortex
```

Then launch a demo in a second terminal:

```bash
./tesseract
```

### OBJ Viewer

Load any `.obj` or `.png` file with automatic zoom-to-fit:

```bash
./viewer path/to/model.obj
```

| Key | Gamepad | Effect |
|-----|---------|--------|
| esc | — | Exit |
| [ / ] | LB / RB | Cycle models |
| — | X | Zoom to fit |
| — | Y | Toggle wireframe |

---

## Custom Demos

Four demos were written specifically for this project to showcase what only a volumetric display can do — depth that a flat screen cannot fake.

### `billboard` — rotating image billboard

Loads one or more PNG images and displays them as a double-sided textured quad that rotates around the Z axis. Glob patterns are accepted, so `./billboard images/*.png` cycles through a folder. Useful for still-frame demos, logos, and album art.

| Key | Gamepad | Effect |
|-----|---------|--------|
| space | A | Toggle auto-rotate |
| `+` / `-` | — | Rotate faster / slower |
| `[` / `]` | LB / RB | Previous / next image |
| — | LT / RT | Zoom out / in |
| — | Right stick Y | Raise / lower billboard |

### `shooter` — volumetric space shooter

Space Invaders reimagined for 3D. The player flies a fully volumetric 7-voxel ship (fuselage, swept wings with dihedral, cockpit bubble, tail fin, thruster plume) through a field of enemies arranged in a classic Space Invaders formation that steps side-to-side and descends toward the player. Three enemy tiers — teal grunts, purple fighters, red-gold elites — each with a distinct 3D silhouette. Elites drop weapon pickups: small spheres carrying a white pixel icon on their player-facing side (three dots in a row = triple-spread, three in a column = piercing laser, quincunx = rapid-fire). Pickups last 12 seconds and tint the ship's body to match.

| Key | Gamepad | Effect |
|-----|---------|--------|
| WASD | Left stick | Move in XY plane |
| Z / X | LT / RT | Descend / ascend |
| space | A / RB | Fire |
| R | — | Restart |
| esc | — | Quit |

### `tunnel` — neon tunnel runner

A cyberpunk infinite runner. The ship is fixed at the near end of a tunnel; seven obstacle types fly toward you from far Y at increasing speed:

- **Ring** — solid ring with a rotating gap
- **Dual ring** — two concentric rings, counter-rotating, gaps misaligned
- **Plus** — four quadrants with a rotating cross-shaped corridor
- **Diamond** — rotating square frame with a gap on one side
- **Slats** — five horizontal bars with a sliding missing slot
- **Iris** — concentric bands with a pulsing open centre
- **Laser** — scanning vertical + horizontal laser lines, find a safe pocket

Each obstacle picks two random colours from an 8-entry neon palette (cyan, magenta, hot pink, electric purple, acid green, neon yellow, ice blue, neon orange) and alternates them across the wall. Obstacles fade in from far-field dim to brilliant as they approach, giving a strong depth cue. Scrolling corner rails and parallax warp stars sell the speed.

| Key | Gamepad | Effect |
|-----|---------|--------|
| WASD | Left stick | Dodge (W/S = up/down, A/D = left/right) |
| space | A / RB | Boost (1.8× speed, particle trail) |
| R | — | Restart |
| esc | — | Quit |

### `vaporcar` — synthwave drive-by scene

A non-interactive vaporwave demoscene: wireframe Ferrari cruising down an endless neon highway. A pulsing striped sun sits above the horizon with concentric cyan / magenta rings and two parallel horizon lines. Road centre-lines, palm trees, holographic cubes, floating diamonds, and double-ring street-lamp pylons scroll past at staggered speeds for parallax depth. Sixteen twinkling stars drift slowly across the sky. The ship trails a white-orange-magenta-purple afterburner out of twin nozzles. All Z coordinates are scaled from the baseline 64-slice design, so the scene fills the whole volume on taller gadgets (e.g. rotovox).

| Key | Effect |
|-----|--------|
| esc | Exit |

---

## Simulator

Run the OpenGL simulator without physical hardware:

```bash
./virtex
```

Useful options:

| Option | Effect |
|--------|--------|
| `-s X` | Angular slice count per revolution |
| `-w X Y` | Panel resolution |
| `-b X` | Bits per channel (1–3) |
| `-g l` | Linear scan geometry (higher quality) |

---

## Custom Gadget Configuration

GPIO pin mapping, panel dimensions, and photo-interrupter pin are defined in:

```
src/driver/gadgets/gadget_<name>.h
```

Edit this file to match your hardware wiring before building.

---

## Acknowledgements

Built on the [Multivox](https://github.com/AncientJames/multivox) open-source volumetric display framework by AncientJames. The DMA display driver, shared-memory voxel buffer architecture, OpenGL simulator, and launcher are derived from that project.
