#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "rammel.h"
#include "input.h"
#include "voxel.h"
#include "timer.h"

#define WORLD_X  VOXELS_X
#define WORLD_Y  VOXELS_Y
#define WORLD_Z  VOXELS_Z
#define CX       ((WORLD_X - 1) * 0.5f)
#define CY       ((WORLD_Y - 1) * 0.5f)
#define CZ       ((WORLD_Z - 1) * 0.5f)

static inline void set_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x < WORLD_X && (uint32_t)y < WORLD_Y && (uint32_t)z < WORLD_Z) {
        vol[VOXEL_INDEX(x, y, z)] = c;
    }
}

static inline void add_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x >= WORLD_X || (uint32_t)y >= WORLD_Y || (uint32_t)z >= WORLD_Z) return;
    pixel_t cur = vol[VOXEL_INDEX(x, y, z)];
    int r = R_PIX(cur) + R_PIX(c); if (r > 255) r = 255;
    int g = G_PIX(cur) + G_PIX(c); if (g > 255) g = 255;
    int b = B_PIX(cur) + B_PIX(c); if (b > 255) b = 255;
    vol[VOXEL_INDEX(x, y, z)] = RGBPIX(r, g, b);
}

static pixel_t dim_pix(pixel_t c, int shift) {
    int r = R_PIX(c) >> shift;
    int g = G_PIX(c) >> shift;
    int b = B_PIX(c) >> shift;
    return RGBPIX(r, g, b);
}

// filled sphere with bright shell + dimmer interior
static void draw_planet(pixel_t* vol, float fx, float fy, float fz, int r, pixel_t shell) {
    int cx = (int)(fx + 0.5f), cy = (int)(fy + 0.5f), cz = (int)(fz + 0.5f);
    if (r < 1) {
        set_voxel(vol, cx, cy, cz, shell);
        return;
    }
    pixel_t fill = dim_pix(shell, 2);
    int r2  = r * r;
    int ri2 = (r - 1) * (r - 1);
    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        int d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > r2) continue;
        pixel_t c = (d2 > ri2) ? shell : fill;
        set_voxel(vol, cx+dx, cy+dy, cz+dz, c);
    }
}

// dim, sparse ring of points showing each planet's orbital path
static void draw_orbit_ring(pixel_t* vol, float radius, float incl, pixel_t c) {
    int steps = (int)(radius * 4.0f);
    if (steps < 32) steps = 32;
    for (int i = 0; i < steps; ++i) {
        // skip every other point to keep the ring subtly dotted
        if (i & 1) continue;
        float a = (float)i / steps * 6.28318530f;
        float x = CX + cosf(a) * radius;
        float y = CY + sinf(a) * radius * cosf(incl);
        float z = CZ + sinf(a) * radius * sinf(incl);
        add_voxel(vol, (int)x, (int)y, (int)z, c);
    }
}

// Saturn ring: points on an annulus tilted around its own axis
static void draw_saturn_ring(pixel_t* vol, float fx, float fy, float fz,
                             float r_in, float r_out, float tilt, pixel_t inner_c, pixel_t outer_c) {
    int steps = 96;
    int bands = 4;
    for (int b = 0; b < bands; ++b) {
        float t = (float)b / (bands - 1);
        float r = r_in + (r_out - r_in) * t;
        // alternate band colours so the ring has visible structure
        pixel_t c = (b & 1) ? outer_c : inner_c;
        for (int i = 0; i < steps; ++i) {
            float a = (float)i / steps * 6.28318530f;
            // ring lies in the planet's local XY plane, then tilted around X axis
            float lx = cosf(a) * r;
            float ly = sinf(a) * r;
            float lz = 0;
            float ty = ly * cosf(tilt) - lz * sinf(tilt);
            float tz = ly * sinf(tilt) + lz * cosf(tilt);
            set_voxel(vol, (int)(fx + lx), (int)(fy + ty), (int)(fz + tz), c);
        }
    }
}

// Comet tail: trail of fading particles along the anti-velocity direction
typedef struct {
    float x, y, z;
    float life;
    bool active;
} tail_t;

#define MAX_TAIL 200
static tail_t comet_tail[MAX_TAIL];
static int    tail_head = 0;

static void comet_emit(float x, float y, float z) {
    tail_t* t = &comet_tail[tail_head];
    t->x = x; t->y = y; t->z = z;
    t->life = 1.0f;
    t->active = true;
    tail_head = (tail_head + 1) % MAX_TAIL;
}

static void comet_update(float dt) {
    for (int i = 0; i < MAX_TAIL; ++i) {
        if (!comet_tail[i].active) continue;
        comet_tail[i].life -= dt * 0.7f;
        if (comet_tail[i].life <= 0) comet_tail[i].active = false;
    }
}

static void comet_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_TAIL; ++i) {
        tail_t* t = &comet_tail[i];
        if (!t->active) continue;
        // tail colour: hot blue-white near comet, fading to cyan-grey
        float k = t->life;
        int r = (int)(180 * k);
        int g = (int)(220 * k);
        int b = (int)(255 * k);
        set_voxel(vol, (int)t->x, (int)t->y, (int)t->z, RGBPIX(r, g, b));
    }
}

// Planet definition. omega is angular velocity (rad/s) — we already pre-scale
// to whatever wall-clock pace looks good rather than using real Kepler periods,
// which would put Neptune at one revolution every several minutes.
typedef struct {
    const char* name;
    float orbit_r;
    float omega;
    float phase;
    float incl;
    int   radius;
    pixel_t colour;
    pixel_t orbit_colour;
} planet_t;

// Visual sizes are exaggerated (Earth-Jupiter ratio is really ~11x; we use ~3x)
// so smaller planets are still visible above one voxel. Colours pick a
// recognisable hue for each body.
static planet_t planets[] = {
    { "Mercury",  9, 0,  0.0f, 0.12f, 1, HEXPIX(AAAAAA), HEXPIX(080808) },
    { "Venus",   14, 0,  0.0f, 0.05f, 2, HEXPIX(EECC88), HEXPIX(0C0A06) },
    { "Earth",   20, 0,  0.0f, 0.00f, 2, HEXPIX(2266FF), HEXPIX(040814) },
    { "Mars",    27, 0,  0.0f, 0.03f, 2, HEXPIX(CC4422), HEXPIX(120604) },
    { "Jupiter", 36, 0,  0.0f, 0.02f, 5, HEXPIX(DD9966), HEXPIX(0C0806) },
    { "Saturn",  46, 0,  0.0f, 0.04f, 4, HEXPIX(EEDD99), HEXPIX(0C0B07) },
    { "Uranus",  54, 0,  0.0f, 0.01f, 3, HEXPIX(99EEEE), HEXPIX(061010) },
    { "Neptune", 60, 0,  0.0f, 0.03f, 3, HEXPIX(3366CC), HEXPIX(040812) },
};
#define NUM_PLANETS ((int)count_of(planets))
#define EARTH_IDX   2
#define SATURN_IDX  5

static float sim_time = 0;

// global rate slider — ESC quits, [/] slow/speed up
static float time_scale = 1.0f;

static void planets_init(void) {
    // omega chosen so Mercury sweeps a full orbit in ~6s and Neptune in ~80s.
    // A pure Kepler scale would make Neptune ~30x slower, which is too slow to
    // watch. We compress that ratio while keeping the relative ordering.
    static const float periods[NUM_PLANETS] = {
        6.0f, 9.0f, 13.0f, 18.0f, 30.0f, 42.0f, 60.0f, 80.0f,
    };
    for (int i = 0; i < NUM_PLANETS; ++i) {
        planets[i].omega = 6.28318530f / periods[i];
        planets[i].phase = (float)i * 0.7f;     // stagger starting positions
    }
}

static void planet_pos(int i, float* x, float* y, float* z) {
    planet_t* p = &planets[i];
    float a = p->phase + p->omega * sim_time;
    *x = CX + cosf(a) * p->orbit_r;
    *y = CY + sinf(a) * p->orbit_r * cosf(p->incl);
    *z = CZ + sinf(a) * p->orbit_r * sinf(p->incl);
}

static void draw_sun(pixel_t* vol) {
    // sun: a 6-voxel radius bright body with a subtly pulsing corona
    int r = 6;
    pixel_t core = HEXPIX(FFEE88);
    pixel_t fill = HEXPIX(FFAA22);
    int r2 = r*r, ri2 = (r-2)*(r-2);
    int icx = (int)CX, icy = (int)CY, icz = (int)CZ;
    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        int d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > r2) continue;
        pixel_t c = (d2 < ri2) ? HEXPIX(FFFFCC) : ((d2 > (r-1)*(r-1)) ? fill : core);
        set_voxel(vol, icx+dx, icy+dy, icz+dz, c);
    }
    // a slow flicker corona — sample a sphere shell of radius r+1..r+2 sparsely
    int n_corona = 64;
    float pulse = 0.5f + 0.5f * sinf(sim_time * 1.7f);
    int cr = (int)(160 + 80 * pulse);
    int cg = (int)(100 + 60 * pulse);
    int cb = (int)(20  + 20 * pulse);
    pixel_t corona = RGBPIX(cr, cg, cb);
    for (int i = 0; i < n_corona; ++i) {
        float a = (float)i / n_corona * 6.28318530f + sim_time * 0.4f;
        for (int j = 0; j < 6; ++j) {
            float b = (float)j / 6.0f * 3.14159265f - 1.5707963f;
            float rr = r + 1.5f + sinf(sim_time * 3.0f + i + j) * 0.3f;
            int x = (int)(CX + cosf(a) * cosf(b) * rr);
            int y = (int)(CY + sinf(a) * cosf(b) * rr);
            int z = (int)(CZ + sinf(b) * rr);
            add_voxel(vol, x, y, z, corona);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do { sleep(1); } while (!voxel_buffer_map());
    }

    timer_init();
    planets_init();
    memset(comet_tail, 0, sizeof(comet_tail));

    printf("=== SOLAR SYSTEM ===\n");
    printf("  [   slow down\n");
    printf("  ]   speed up\n");
    printf("  ESC quit\n\n");

    input_set_nonblocking();

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);

        switch (ch) {
            case '[': time_scale *= 0.7f; if (time_scale < 0.05f) time_scale = 0.05f; break;
            case ']': time_scale *= 1.4f; if (time_scale > 8.0f)  time_scale = 8.0f;  break;
        }

        sim_time += dt * time_scale;
        comet_update(dt);

        // comet on a wide elliptical orbit — emits trail every frame
        float ct = sim_time * 0.18f;
        float ce = 0.7f;                              // eccentricity
        float ca = 55.0f;                             // semi-major
        float cb = ca * sqrtf(1.0f - ce*ce);          // semi-minor
        float cx_pos = CX + cosf(ct) * ca - ce * ca;
        float cy_pos = CY + sinf(ct) * cb * cosf(0.6f);
        float cz_pos = CZ + sinf(ct) * cb * sinf(0.6f);
        comet_emit(cx_pos, cy_pos, cz_pos);

        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        // dim orbit guide rings under everything else
        for (int i = 0; i < NUM_PLANETS; ++i) {
            draw_orbit_ring(vol, planets[i].orbit_r, planets[i].incl,
                            planets[i].orbit_colour);
        }

        draw_sun(vol);
        comet_draw(vol);

        // comet head
        draw_planet(vol, cx_pos, cy_pos, cz_pos, 1, HEXPIX(EEFFFF));

        for (int i = 0; i < NUM_PLANETS; ++i) {
            float x, y, z;
            planet_pos(i, &x, &y, &z);
            draw_planet(vol, x, y, z, planets[i].radius, planets[i].colour);

            if (i == EARTH_IDX) {
                // Moon: small grey body orbiting Earth
                float ma = sim_time * 1.6f;
                float mr = 4.5f;
                float mx = x + cosf(ma) * mr;
                float my = y + sinf(ma) * mr * 0.95f;
                float mz = z + sinf(ma) * mr * 0.05f;
                draw_planet(vol, mx, my, mz, 1, HEXPIX(BBBBBB));
            }
            if (i == SATURN_IDX) {
                draw_saturn_ring(vol, x, y, z, 5.5f, 8.5f, 0.45f,
                                 HEXPIX(BBAA66), HEXPIX(776633));
            }
        }

        voxel_buffer_swap();

        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();
    return 0;
}
