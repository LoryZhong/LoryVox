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

// additive blend so overlapping particles brighten instead of overwriting
static inline void add_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x >= WORLD_X || (uint32_t)y >= WORLD_Y || (uint32_t)z >= WORLD_Z) return;
    pixel_t cur = vol[VOXEL_INDEX(x, y, z)];
    int r = R_PIX(cur) + R_PIX(c); if (r > 255) r = 255;
    int g = G_PIX(cur) + G_PIX(c); if (g > 255) g = 255;
    int b = B_PIX(cur) + B_PIX(c); if (b > 255) b = 255;
    vol[VOXEL_INDEX(x, y, z)] = RGBPIX(r, g, b);
}

static float rand_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

// uniformly random unit vector (Marsaglia method)
static void rand_unit(float* x, float* y, float* z) {
    while (1) {
        float u = rand_f(-1.0f, 1.0f);
        float v = rand_f(-1.0f, 1.0f);
        float s = u*u + v*v;
        if (s >= 1.0f || s == 0.0f) continue;
        float f = 2.0f * sqrtf(1.0f - s);
        *x = u * f;
        *y = v * f;
        *z = 1.0f - 2.0f * s;
        return;
    }
}

// Five 16-step temperature gradients, hot at index 0 to cold at 15.
// Picking different palettes per supernova gives the show colour variety.
#define PALETTE_LEN 16
#define NUM_PALETTES 5

static const pixel_t palettes[NUM_PALETTES][PALETTE_LEN] = {
    // 0: classic supernova (white -> yellow -> red -> magenta -> dark)
    {
        HEXPIX(FFFFFF), HEXPIX(FFF8CC), HEXPIX(FFE077), HEXPIX(FFC033),
        HEXPIX(FF9911), HEXPIX(FF6600), HEXPIX(FF3300), HEXPIX(EE1144),
        HEXPIX(CC0066), HEXPIX(990077), HEXPIX(660066), HEXPIX(440055),
        HEXPIX(220044), HEXPIX(110022), HEXPIX(080011), HEXPIX(000000),
    },
    // 1: hot blue (rare hypernova-look)
    {
        HEXPIX(FFFFFF), HEXPIX(CCEEFF), HEXPIX(88CCFF), HEXPIX(44AAFF),
        HEXPIX(2288EE), HEXPIX(1166CC), HEXPIX(0044AA), HEXPIX(442299),
        HEXPIX(661188), HEXPIX(881177), HEXPIX(660055), HEXPIX(440044),
        HEXPIX(220033), HEXPIX(110022), HEXPIX(080011), HEXPIX(000000),
    },
    // 2: green nebula
    {
        HEXPIX(FFFFFF), HEXPIX(DDFFEE), HEXPIX(99FFCC), HEXPIX(55EEAA),
        HEXPIX(22DD88), HEXPIX(11AA66), HEXPIX(008855), HEXPIX(006666),
        HEXPIX(005577), HEXPIX(004488), HEXPIX(003366), HEXPIX(002255),
        HEXPIX(001144), HEXPIX(000833), HEXPIX(000422), HEXPIX(000000),
    },
    // 3: violet / pink crystal
    {
        HEXPIX(FFFFFF), HEXPIX(FFDDFF), HEXPIX(FFAAEE), HEXPIX(FF77DD),
        HEXPIX(EE44CC), HEXPIX(CC22BB), HEXPIX(9911AA), HEXPIX(770099),
        HEXPIX(550088), HEXPIX(440077), HEXPIX(330066), HEXPIX(220055),
        HEXPIX(110044), HEXPIX(080033), HEXPIX(040022), HEXPIX(000000),
    },
    // 4: warm gold (orange-amber dominant)
    {
        HEXPIX(FFFFEE), HEXPIX(FFEEAA), HEXPIX(FFDD66), HEXPIX(FFBB22),
        HEXPIX(FF9900), HEXPIX(EE7700), HEXPIX(CC5500), HEXPIX(AA3300),
        HEXPIX(881122), HEXPIX(660033), HEXPIX(440033), HEXPIX(330022),
        HEXPIX(220011), HEXPIX(110008), HEXPIX(080004), HEXPIX(000000),
    },
};

static pixel_t pal_color(int p, float t) {
    if (t < 0) t = 0; if (t > 0.999f) t = 0.999f;
    int i = (int)(t * PALETTE_LEN);
    return palettes[p][i];
}

// Particles
#define MAX_PARTICLES 1600

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float age, life;
    uint8_t palette;
    uint8_t kind;     // 0 = shockwave dust, 1 = jet streak, 2 = ember
    bool active;
} particle_t;

static particle_t particles[MAX_PARTICLES];

static int particle_alloc(void) {
    static int hint = 0;
    for (int n = 0; n < MAX_PARTICLES; ++n) {
        int i = (hint + n) % MAX_PARTICLES;
        if (!particles[i].active) {
            hint = (i + 1) % MAX_PARTICLES;
            return i;
        }
    }
    return -1;
}

// Supernova events
#define MAX_NOVAE 4

enum { N_CHARGE, N_FLASH, N_EXPAND, N_FADE, N_DONE };

typedef struct {
    float cx, cy, cz;
    float t;          // seconds since start
    float duration_charge, duration_flash;
    int   phase;
    int   palette;
    float scale;      // 1.0 = big central nova; 0.5 = small satellite
    int   jets;       // number of jet directions to spawn
    bool  active;
} nova_t;

static nova_t novae[MAX_NOVAE];

static void spawn_jet(nova_t* n, float dx, float dy, float dz) {
    int beads = (int)(40 * n->scale);
    for (int i = 0; i < beads; ++i) {
        int idx = particle_alloc();
        if (idx < 0) return;
        particle_t* p = &particles[idx];
        p->active = true;
        p->kind = 1;
        p->palette = n->palette;
        p->x = n->cx; p->y = n->cy; p->z = n->cz;
        // jet beads share the same direction with a tight cone of jitter
        float jitter = 0.08f;
        float vx = dx + rand_f(-jitter, jitter);
        float vy = dy + rand_f(-jitter, jitter);
        float vz = dz + rand_f(-jitter, jitter);
        float speed = rand_f(40.0f, 95.0f) * n->scale;
        p->vx = vx * speed;
        p->vy = vy * speed;
        p->vz = vz * speed;
        p->age = 0;
        p->life = rand_f(1.4f, 2.4f);
    }
}

static void spawn_shockwave(nova_t* n) {
    int count = (int)(700 * n->scale);
    for (int i = 0; i < count; ++i) {
        int idx = particle_alloc();
        if (idx < 0) return;
        particle_t* p = &particles[idx];
        p->active = true;
        p->kind = 0;
        p->palette = n->palette;
        p->x = n->cx; p->y = n->cy; p->z = n->cz;
        float dx, dy, dz;
        rand_unit(&dx, &dy, &dz);
        // velocity magnitude varies — gives the cloud a thick shell, not paper-thin
        float speed = rand_f(15.0f, 70.0f) * n->scale;
        p->vx = dx * speed;
        p->vy = dy * speed;
        p->vz = dz * speed;
        p->age = 0;
        p->life = rand_f(1.6f, 3.4f);
    }

    // pick `n->jets` random directions for bright streaks
    for (int j = 0; j < n->jets; ++j) {
        float dx, dy, dz;
        rand_unit(&dx, &dy, &dz);
        spawn_jet(n, dx, dy, dz);
    }
}

static void spawn_embers(nova_t* n) {
    int count = (int)(120 * n->scale);
    for (int i = 0; i < count; ++i) {
        int idx = particle_alloc();
        if (idx < 0) return;
        particle_t* p = &particles[idx];
        p->active = true;
        p->kind = 2;
        p->palette = n->palette;
        // embers seed inside an already-expanded cloud
        float dx, dy, dz;
        rand_unit(&dx, &dy, &dz);
        float r = rand_f(2.0f, 18.0f * n->scale);
        p->x = n->cx + dx * r;
        p->y = n->cy + dy * r;
        p->z = n->cz + dz * r;
        p->vx = dx * rand_f(2.0f, 10.0f);
        p->vy = dy * rand_f(2.0f, 10.0f);
        p->vz = dz * rand_f(2.0f, 10.0f);
        p->age = rand_f(0.0f, 0.4f);
        p->life = rand_f(2.5f, 4.5f);
    }
}

static void nova_start(nova_t* n, float x, float y, float z, float scale) {
    n->cx = x; n->cy = y; n->cz = z;
    n->t = 0;
    n->scale = scale;
    n->duration_charge = rand_f(0.8f, 1.6f) * (0.5f + 0.5f * scale);
    n->duration_flash  = 0.18f;
    n->phase   = N_CHARGE;
    n->palette = rand() % NUM_PALETTES;
    n->jets    = (scale > 0.7f) ? 2 + (rand() % 4) : 0;   // small ones don't spike
    n->active  = true;
}

static int nova_alloc(void) {
    for (int i = 0; i < MAX_NOVAE; ++i) if (!novae[i].active) return i;
    return -1;
}

static void nova_update(nova_t* n, float dt) {
    n->t += dt;
    switch (n->phase) {
    case N_CHARGE:
        if (n->t >= n->duration_charge) {
            n->phase = N_FLASH;
            n->t = 0;
            spawn_shockwave(n);
            spawn_embers(n);
        }
        break;
    case N_FLASH:
        if (n->t >= n->duration_flash) {
            n->phase = N_EXPAND;
            n->t = 0;
        }
        break;
    case N_EXPAND:
        if (n->t >= 2.5f) n->phase = N_FADE;
        break;
    case N_FADE:
        if (n->t >= 4.5f) { n->phase = N_DONE; n->active = false; }
        break;
    }
}

static void nova_draw_core(pixel_t* vol, nova_t* n) {
    // CHARGE: small bright pulsating core that swells slightly before detonating.
    // FLASH: a brilliant solid flash that briefly washes the centre.
    int cx = (int)n->cx, cy = (int)n->cy, cz = (int)n->cz;

    if (n->phase == N_CHARGE) {
        float k = n->t / n->duration_charge;        // 0..1
        float pulse = 0.5f + 0.5f * sinf(n->t * 18.0f);
        int rad = (int)(2.0f + 3.0f * k * n->scale);
        pixel_t hot = pal_color(n->palette, 0.0f);
        // dim by pulse so it 'breathes'
        int rr = (int)(R_PIX(hot) * (0.4f + 0.6f * pulse));
        int gg = (int)(G_PIX(hot) * (0.4f + 0.6f * pulse));
        int bb = (int)(B_PIX(hot) * (0.4f + 0.6f * pulse));
        pixel_t c = RGBPIX(rr, gg, bb);
        for (int dx = -rad; dx <= rad; ++dx)
        for (int dy = -rad; dy <= rad; ++dy)
        for (int dz = -rad; dz <= rad; ++dz) {
            if (dx*dx + dy*dy + dz*dz <= rad*rad) {
                add_voxel(vol, cx+dx, cy+dy, cz+dz, c);
            }
        }
    } else if (n->phase == N_FLASH) {
        float k = 1.0f - (n->t / n->duration_flash);
        int rad = (int)(8.0f + 14.0f * n->scale * k);
        pixel_t white = HEXPIX(FFFFFF);
        int rr = (int)(R_PIX(white) * k);
        int gg = (int)(G_PIX(white) * k);
        int bb = (int)(B_PIX(white) * k);
        pixel_t c = RGBPIX(rr, gg, bb);
        int r2 = rad * rad;
        for (int dx = -rad; dx <= rad; ++dx)
        for (int dy = -rad; dy <= rad; ++dy)
        for (int dz = -rad; dz <= rad; ++dz) {
            int d2 = dx*dx + dy*dy + dz*dz;
            if (d2 <= r2) add_voxel(vol, cx+dx, cy+dy, cz+dz, c);
        }
    }
}

static void particles_update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        // mild drag — particles slow as they expand
        p->vx *= 0.985f;
        p->vy *= 0.985f;
        p->vz *= 0.985f;
        p->age += dt;
        if (p->age >= p->life ||
            p->x < 0 || p->x >= WORLD_X ||
            p->y < 0 || p->y >= WORLD_Y ||
            p->z < 0 || p->z >= WORLD_Z) {
            p->active = false;
        }
    }
}

static void particles_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;
        float t = p->age / p->life;
        pixel_t c = pal_color(p->palette, t);

        if (p->kind == 1) {
            // jet streak: head plus a short fading trail along velocity
            int hx = (int)p->x, hy = (int)p->y, hz = (int)p->z;
            add_voxel(vol, hx, hy, hz, c);
            float speed = sqrtf(p->vx*p->vx + p->vy*p->vy + p->vz*p->vz);
            if (speed > 1.0f) {
                float ux = -p->vx / speed, uy = -p->vy / speed, uz = -p->vz / speed;
                pixel_t cd = pal_color(p->palette, t + 0.08f);
                add_voxel(vol, (int)(p->x + ux), (int)(p->y + uy), (int)(p->z + uz), cd);
            }
        } else {
            add_voxel(vol, (int)p->x, (int)p->y, (int)p->z, c);
        }
    }
}

// Director: keep the show busy. One big nova at a time near centre,
// plus smaller ones scattered around.
static float next_big_in   = 1.0f;
static float next_small_in = 0.5f;

static void director_update(float dt) {
    next_big_in   -= dt;
    next_small_in -= dt;

    if (next_big_in <= 0) {
        int i = nova_alloc();
        if (i >= 0) {
            float rmax = 18.0f;
            nova_start(&novae[i],
                CX + rand_f(-rmax, rmax),
                CY + rand_f(-rmax, rmax),
                CZ + rand_f(-6.0f, 6.0f),
                rand_f(0.85f, 1.1f));
        }
        next_big_in = rand_f(7.0f, 11.0f);
    }
    if (next_small_in <= 0) {
        int i = nova_alloc();
        if (i >= 0) {
            nova_start(&novae[i],
                rand_f(15.0f, WORLD_X - 15.0f),
                rand_f(15.0f, WORLD_Y - 15.0f),
                rand_f(8.0f,  WORLD_Z - 8.0f),
                rand_f(0.35f, 0.6f));
        }
        next_small_in = rand_f(2.0f, 4.5f);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do { sleep(1); } while (!voxel_buffer_map());
    }

    timer_init();
    memset(particles, 0, sizeof(particles));
    memset(novae, 0, sizeof(novae));

    printf("=== NOVA ===\n");
    printf("  ESC quit\n\n");

    input_set_nonblocking();

    // kick off the first one immediately so the user sees something at startup
    nova_start(&novae[0], CX, CY, CZ, 1.0f);

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);

        director_update(dt);
        for (int i = 0; i < MAX_NOVAE; ++i) {
            if (novae[i].active) nova_update(&novae[i], dt);
        }
        particles_update(dt);

        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        particles_draw(vol);
        for (int i = 0; i < MAX_NOVAE; ++i) {
            if (novae[i].active) nova_draw_core(vol, &novae[i]);
        }

        voxel_buffer_swap();

        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();
    return 0;
}
