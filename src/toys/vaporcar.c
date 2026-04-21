#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>

#include "array.h"
#include "rammel.h"
#include "input.h"
#include "graphics.h"
#include "voxel.h"
#include "timer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CX (VOXELS_X / 2)

// scale a value authored for a 64-high display (vortex) up to whatever Z the
// current gadget actually uses (rotovox is 128).
#define ZS(v) ((v) * (float)VOXELS_Z / 64.0f)

#define ROAD_Z ZS(16.0f)

typedef struct {
    float x;
    float y;
    float z;
} point3_t;

static const pixel_t COL_CYAN      = HEXPIX(00FFFF);
static const pixel_t COL_MINT      = HEXPIX(88FFFF);
static const pixel_t COL_MAGENTA   = HEXPIX(FF2DAA);
static const pixel_t COL_PINK      = HEXPIX(FF66CC);
static const pixel_t COL_PURPLE    = HEXPIX(9B30FF);
static const pixel_t COL_ORANGE    = HEXPIX(FF9A33);
static const pixel_t COL_GOLD      = HEXPIX(FFD966);
static const pixel_t COL_WHITE     = HEXPIX(FFFFFF);
static const pixel_t COL_BLUE      = HEXPIX(3A66FF);
static const pixel_t COL_DIM_BLUE  = HEXPIX(2244AA);
static const pixel_t COL_RED       = HEXPIX(FF3344);

static const point3_t STAR_SEEDS[] = {
    {-44,  30, 51}, {-35,  44, 58}, {-24,  64, 52}, {-11,  84, 56},
    {  8,  99, 50}, { 25,  82, 57}, { 37,  61, 53}, { 46,  35, 59},
    {-41,  92, 49}, {-18, 110, 54}, { 17, 112, 55}, { 41,  94, 51},
    {-52,  70, 47}, { 54,  73, 48}, {-30,  20, 55}, { 29,  17, 57}
};

static inline bool inside(int x, int y, int z) {
    return x >= 0 && x < VOXELS_X && y >= 0 && y < VOXELS_Y && z >= 0 && z < VOXELS_Z;
}

static inline void put_voxel(pixel_t* volume, int x, int y, int z, pixel_t colour) {
    if (!inside(x, y, z)) return;
    if (!voxel_in_cylinder(x, y)) return;
    volume[VOXEL_INDEX(x, y, z)] = colour;
}

static void draw_line3(pixel_t* volume, point3_t a, point3_t b, pixel_t colour) {
    float va[3] = {a.x, a.y, a.z};
    float vb[3] = {b.x, b.y, b.z};
    graphics_draw_line(volume, va, vb, colour);
}

static void draw_disc_y(pixel_t* volume, float cx, float y, float cz, float radius, pixel_t colour, bool striped) {
    int r = (int)ceilf(radius);
    for (int dx = -r; dx <= r; ++dx) {
        for (int dz = -r; dz <= r; ++dz) {
            float dist2 = (float)(dx * dx + dz * dz);
            if (dist2 > radius * radius) continue;
            int z = (int)lroundf(cz + dz);
            if (striped) {
                int band = ((int)floorf((z - (cz - radius)) / 3.0f)) & 1;
                if (band == 0) continue;
            }
            put_voxel(volume, (int)lroundf(cx + dx), (int)lroundf(y), z, colour);
        }
    }
}

static void draw_ring_y(pixel_t* volume, float cx, float y, float cz, float radius, pixel_t colour) {
    int steps = (int)(radius * 10.0f);
    if (steps < 24) steps = 24;
    for (int i = 0; i < steps; ++i) {
        float a = 2.0f * (float)M_PI * i / (float)steps;
        int x = (int)lroundf(cx + cosf(a) * radius);
        int z = (int)lroundf(cz + sinf(a) * radius);
        put_voxel(volume, x, (int)lroundf(y), z, colour);
    }
}

static void draw_sphere(pixel_t* volume, float x, float y, float z, float radius, pixel_t colour) {
    int r = (int)ceilf(radius);
    float rsq = radius * radius;
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dz = -r; dz <= r; ++dz) {
                float dsq = (float)(dx * dx + dy * dy + dz * dz);
                if (dsq <= rsq) {
                    put_voxel(volume, (int)lroundf(x + dx), (int)lroundf(y + dy), (int)lroundf(z + dz), colour);
                }
            }
        }
    }
}

static void draw_box_wire(pixel_t* volume, point3_t c, float sx, float sy, float sz, pixel_t colour) {
    point3_t p[8] = {
        {c.x - sx, c.y - sy, c.z - sz}, {c.x + sx, c.y - sy, c.z - sz},
        {c.x + sx, c.y + sy, c.z - sz}, {c.x - sx, c.y + sy, c.z - sz},
        {c.x - sx, c.y - sy, c.z + sz}, {c.x + sx, c.y - sy, c.z + sz},
        {c.x + sx, c.y + sy, c.z + sz}, {c.x - sx, c.y + sy, c.z + sz},
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
    };
    for (unsigned i = 0; i < 12; ++i) draw_line3(volume, p[edges[i][0]], p[edges[i][1]], colour);
}

static void draw_diamond(pixel_t* volume, point3_t c, float s, pixel_t colour) {
    point3_t top    = {c.x, c.y, c.z + s};
    point3_t bottom = {c.x, c.y, c.z - s};
    point3_t east   = {c.x + s, c.y, c.z};
    point3_t west   = {c.x - s, c.y, c.z};
    point3_t north  = {c.x, c.y + s, c.z};
    point3_t south  = {c.x, c.y - s, c.z};
    draw_line3(volume, top, east, colour);    draw_line3(volume, top, west, colour);
    draw_line3(volume, top, north, colour);   draw_line3(volume, top, south, colour);
    draw_line3(volume, bottom, east, colour); draw_line3(volume, bottom, west, colour);
    draw_line3(volume, bottom, north, colour); draw_line3(volume, bottom, south, colour);
}

static void draw_stars(pixel_t* volume, float t) {
    for (unsigned i = 0; i < sizeof(STAR_SEEDS) / sizeof(STAR_SEEDS[0]); ++i) {
        float pulse = 0.5f + 0.5f * sinf(t * 1.3f + (float)i);
        pixel_t c = pulse > 0.72f ? COL_WHITE : (pulse > 0.35f ? COL_MINT : COL_DIM_BLUE);

        // slow parallax drift — stars drift back much slower than the road
        float drift = fmodf(STAR_SEEDS[i].y - t * 5.0f, (float)VOXELS_Y);
        if (drift < 0) drift += (float)VOXELS_Y;

        int x = (int)lroundf(CX + STAR_SEEDS[i].x);
        int y = (int)lroundf(drift);
        int z = (int)lroundf(ZS(STAR_SEEDS[i].z) + ZS(1.0f) * sinf(t * 0.7f + i * 0.37f));
        put_voxel(volume, x, y, z, c);
    }
}

static void draw_sun(pixel_t* volume, float t) {
    float pulse = sinf(t * 0.7f) * 1.2f;
    float y = 104.0f;
    float z = ZS(40.0f) + pulse;
    float radius = 18.0f + 1.0f * sinf(t * 0.35f);

    draw_disc_y(volume, CX, y, z, radius + 3.5f, COL_PURPLE, false);
    draw_disc_y(volume, CX, y, z, radius, COL_ORANGE, true);
    draw_ring_y(volume, CX, y, z, radius + 4.5f, COL_PINK);
    draw_ring_y(volume, CX, y, z, radius + 7.0f, COL_CYAN);

    draw_line3(volume, (point3_t){18.0f, 92.0f, ZS(27.0f)}, (point3_t){110.0f, 92.0f, ZS(27.0f)}, COL_CYAN);
    draw_line3(volume, (point3_t){22.0f, 92.0f, ZS(28.0f)}, (point3_t){106.0f, 92.0f, ZS(28.0f)}, COL_MAGENTA);
}

static void draw_road(pixel_t* volume, float t) {
    float base_z = ROAD_Z + 0.35f * sinf(t * 2.0f);
    point3_t a = {47.0f, 6.0f, base_z};
    point3_t b = {81.0f, 6.0f, base_z};
    point3_t c = {57.0f, 124.0f, base_z + 1.0f};
    point3_t d = {71.0f, 124.0f, base_z + 1.0f};
    draw_line3(volume, a, c, COL_CYAN);
    draw_line3(volume, b, d, COL_CYAN);
    draw_line3(volume, (point3_t){49.0f, 6.0f, base_z + 1.0f}, (point3_t){58.0f, 124.0f, base_z + 2.0f}, COL_BLUE);
    draw_line3(volume, (point3_t){79.0f, 6.0f, base_z + 1.0f}, (point3_t){70.0f, 124.0f, base_z + 2.0f}, COL_BLUE);

    for (int i = 0; i < 12; ++i) {
        float phase = fmodf(t * 22.0f + i * 9.0f, 118.0f);
        float y = 6.0f + phase;
        float near = y / 124.0f;
        float halfw = 17.0f - near * 11.5f;
        float z = base_z + near * 1.3f;
        pixel_t ccol = (i & 1) ? COL_MAGENTA : COL_PURPLE;
        draw_line3(volume, (point3_t){CX - halfw, y, z}, (point3_t){CX + halfw, y, z}, ccol);
    }

    for (int y = 12; y < 124; y += 8) {
        float near = y / 124.0f;
        float halfw = 19.0f - near * 13.0f;
        float z = base_z - 2.0f + near * 1.5f;
        draw_line3(volume, (point3_t){CX - halfw, y, z}, (point3_t){CX + halfw, y, z}, COL_DIM_BLUE);
    }
}

static void draw_particle_trails(pixel_t* volume, float t, float x, float y, float z) {
    for (int side = -1; side <= 1; side += 2) {
        float nozzle_x = x + side * 6.8f;
        float nozzle_y = y - 6.2f;
        float nozzle_z = z + 0.5f;
        for (int i = 0; i < 14; ++i) {
            float phase = t * 18.0f + i * 0.85f + (side > 0 ? 1.7f : 0.0f);
            float px = nozzle_x + side * (0.7f + 0.25f * i) + 0.7f * sinf(phase * 0.7f);
            float py = nozzle_y - 2.6f - i * 2.8f + 0.8f * sinf(phase * 1.5f);
            float pz = nozzle_z + 0.9f * sinf(phase * 1.2f) + 0.16f * i;
            pixel_t c = (i < 4) ? COL_WHITE : ((i < 8) ? COL_ORANGE : ((i < 11) ? COL_MAGENTA : COL_PURPLE));
            float r = (i < 3) ? 1.3f : (i < 8 ? 1.0f : 0.8f);
            draw_sphere(volume, px, py, pz, r, c);
        }
    }
}

static void draw_ferrari_car(pixel_t* volume, float t) {
    float bob = 0.55f * sinf(t * 3.4f);
    float x = (float)CX;
    float y = 26.0f;
    float z = ZS(20.0f) + bob;

    point3_t lower[8] = {
        {x - 9.5f, y - 7.0f, z - 2.0f}, {x + 9.5f, y - 7.0f, z - 2.0f},
        {x + 9.0f, y + 7.0f, z - 2.0f}, {x - 9.0f, y + 7.0f, z - 2.0f},
        {x - 9.5f, y - 7.0f, z + 2.0f}, {x + 9.5f, y - 7.0f, z + 2.0f},
        {x + 9.0f, y + 7.0f, z + 2.0f}, {x - 9.0f, y + 7.0f, z + 2.0f},
    };
    static const int box_edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
    };
    for (unsigned i = 0; i < 12; ++i) draw_line3(volume, lower[box_edges[i][0]], lower[box_edges[i][1]], COL_RED);

    point3_t roof[8] = {
        {x - 4.8f, y - 2.0f, z + 2.0f}, {x + 4.8f, y - 2.0f, z + 2.0f},
        {x + 2.8f, y + 3.0f, z + 2.0f}, {x - 2.8f, y + 3.0f, z + 2.0f},
        {x - 4.3f, y - 1.0f, z + 5.4f}, {x + 4.3f, y - 1.0f, z + 5.4f},
        {x + 2.2f, y + 2.4f, z + 4.8f}, {x - 2.2f, y + 2.4f, z + 4.8f},
    };
    for (unsigned i = 0; i < 12; ++i) draw_line3(volume, roof[box_edges[i][0]], roof[box_edges[i][1]], COL_WHITE);

    draw_line3(volume, (point3_t){x - 9.5f, y + 7.0f, z + 0.0f}, (point3_t){x - 5.8f, y + 11.0f, z + 0.8f}, COL_RED);
    draw_line3(volume, (point3_t){x + 9.5f, y + 7.0f, z + 0.0f}, (point3_t){x + 5.8f, y + 11.0f, z + 0.8f}, COL_RED);
    draw_line3(volume, (point3_t){x - 5.8f, y + 11.0f, z + 0.8f}, (point3_t){x + 5.8f, y + 11.0f, z + 0.8f}, COL_MAGENTA);
    draw_line3(volume, (point3_t){x - 8.8f, y - 7.0f, z + 0.2f}, (point3_t){x - 4.5f, y - 9.6f, z + 1.4f}, COL_RED);
    draw_line3(volume, (point3_t){x + 8.8f, y - 7.0f, z + 0.2f}, (point3_t){x + 4.5f, y - 9.6f, z + 1.4f}, COL_RED);
    draw_line3(volume, (point3_t){x - 4.5f, y - 9.6f, z + 1.4f}, (point3_t){x + 4.5f, y - 9.6f, z + 1.4f}, COL_PINK);

    draw_line3(volume, (point3_t){x - 3.8f, y + 2.4f, z + 4.8f}, (point3_t){x + 3.8f, y + 2.4f, z + 4.8f}, COL_CYAN);
    draw_line3(volume, (point3_t){x - 4.0f, y - 1.5f, z + 4.9f}, (point3_t){x + 4.0f, y - 1.5f, z + 4.9f}, COL_CYAN);
    draw_line3(volume, (point3_t){x - 6.2f, y - 0.5f, z + 2.5f}, (point3_t){x - 1.2f, y + 5.0f, z + 4.3f}, COL_MINT);
    draw_line3(volume, (point3_t){x + 6.2f, y - 0.5f, z + 2.5f}, (point3_t){x + 1.2f, y + 5.0f, z + 4.3f}, COL_MINT);

    draw_sphere(volume, x - 7.6f, y - 4.8f, z - 1.7f, 1.5f, COL_CYAN);
    draw_sphere(volume, x + 7.6f, y - 4.8f, z - 1.7f, 1.5f, COL_CYAN);
    draw_sphere(volume, x - 7.3f, y + 4.8f, z - 1.8f, 1.2f, COL_GOLD);
    draw_sphere(volume, x + 7.3f, y + 4.8f, z - 1.8f, 1.2f, COL_GOLD);

    draw_line3(volume, (point3_t){x - 9.2f, y + 0.0f, z - 2.0f}, (point3_t){x + 9.2f, y + 0.0f, z - 2.0f}, COL_PURPLE);
    draw_particle_trails(volume, t, x, y, z);
}

static void draw_palm_fronds(pixel_t* volume, float base_x, float base_y, float base_z, int side, float sway) {
    point3_t crown = {base_x, base_y, base_z + 10.0f};
    for (int i = 0; i < 5; ++i) {
        float angle = -0.9f + i * 0.45f + sway;
        point3_t tip = {
            base_x + side * (8.0f + 1.5f * i),
            base_y + 6.0f + i * 2.2f,
            base_z + 10.0f + 3.5f * sinf(angle)
        };
        draw_line3(volume, crown, tip, COL_MAGENTA);
        draw_line3(volume, (point3_t){crown.x, crown.y, crown.z - 0.8f}, (point3_t){tip.x, tip.y - 1.5f, tip.z - 0.6f}, COL_PINK);
    }
    for (int i = 0; i < 3; ++i) {
        float angle = 0.4f + i * 0.4f - sway * 0.7f;
        point3_t tip = {
            base_x - side * (6.5f + 1.5f * i),
            base_y + 6.0f + i * 2.3f,
            base_z + 9.5f + 3.2f * sinf(angle)
        };
        draw_line3(volume, crown, tip, COL_MAGENTA);
    }
}

static void draw_palm_tree(pixel_t* volume, float x, float y, float z, int side, float t, int idx) {
    float sway = 0.22f * sinf(t * 1.6f + idx * 0.8f + (side > 0 ? 0.7f : 0.0f));
    point3_t trunk0 = {x, y, z};
    point3_t trunk1 = {x + side * 0.8f, y + 5.0f, z + 2.0f};
    point3_t trunk2 = {x + side * 1.4f + sway, y + 10.0f, z + 4.2f};
    point3_t trunk3 = {x + side * 2.2f + sway * 1.6f, y + 15.0f, z + 6.5f};
    point3_t trunk4 = {x + side * 2.8f + sway * 2.2f, y + 20.0f, z + 8.2f};
    draw_line3(volume, trunk0, trunk1, COL_ORANGE);
    draw_line3(volume, trunk1, trunk2, COL_ORANGE);
    draw_line3(volume, trunk2, trunk3, COL_GOLD);
    draw_line3(volume, trunk3, trunk4, COL_GOLD);
    draw_palm_fronds(volume, trunk4.x, trunk4.y, z, side, sway);
}

// Scroll a prop forward from y=124 (far) to y=4 (near) at `speed` voxels/sec.
// Each instance is offset by (i * range / count) so the stream stays populated.
static float scroll_y(float t, int i, float speed, float range, int count) {
    float y = fmodf(t * speed + (float)i * range / (float)count, range);
    if (y < 0) y += range;
    return range - y + 4.0f;  // far → near
}

static void draw_side_props(pixel_t* volume, float t) {
    for (int side = -1; side <= 1; side += 2) {
        float xbase = (float)CX + side * 32.0f;

        // holo-cubes (4 per side, parallax speed 20)
        for (int i = 0; i < 4; ++i) {
            float y = scroll_y(t, i, 20.0f, 120.0f, 4);
            float z = ZS(23.0f) + 3.0f * sinf(t + i * 0.7f + (side > 0 ? 0.5f : 0.0f));
            draw_box_wire(volume, (point3_t){xbase + side * 8.0f, y, z}, 4.2f, 4.2f, 4.2f,
                          (side < 0) ? COL_PURPLE : COL_CYAN);
        }

        // floating diamonds (3 per side, higher up, speed 22)
        for (int i = 0; i < 3; ++i) {
            float y = scroll_y(t, i, 22.0f, 120.0f, 3);
            float pulse = 1.8f * sinf(t * 1.7f + i + (side > 0 ? 1.0f : 0.0f));
            draw_diamond(volume, (point3_t){xbase + side * 14.0f, y, ZS(34.0f) + pulse}, 4.5f, COL_MAGENTA);
        }

        // street-lamp double-ring pylons (3 per side, speed 24 — nearest = fastest)
        for (int i = 0; i < 3; ++i) {
            float y = scroll_y(t, i, 24.0f, 120.0f, 3);
            float base = ZS(10.0f + i * 2.0f);
            draw_ring_y(volume, xbase + side * 18.0f, y, base + ZS(2.0f) * i,
                        3.5f + 0.3f * sinf(t * 2.0f + i), COL_PINK);
            draw_ring_y(volume, xbase + side * 18.0f, y, base + ZS(6.0f) + ZS(2.0f) * i,
                        3.5f + 0.3f * sinf(t * 2.0f + i), COL_PINK);
            draw_line3(volume, (point3_t){xbase + side * 18.0f, y, base},
                       (point3_t){xbase + side * 18.0f, y, base + ZS(8.0f)}, COL_PURPLE);
        }

        // palm trees (3 per side, speed 18 — background depth)
        for (int i = 0; i < 3; ++i) {
            float y = scroll_y(t, i, 18.0f, 124.0f, 3);
            draw_palm_tree(volume, xbase + side * 4.0f, y, ZS(18.0f), side, t, i);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!voxel_buffer_map()) {
        return 1;
    }

    timer_init();
    input_set_nonblocking();

    float t = 0.0f;

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);
        t += dt;

        pixel_t* volume = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(volume);

        draw_stars(volume, t);
        draw_sun(volume, t);
        draw_side_props(volume, t);
        draw_road(volume, t);
        draw_ferrari_car(volume, t);

        voxel_buffer_swap();
        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();
    return 0;
}
