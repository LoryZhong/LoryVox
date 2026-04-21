#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

#include "mathc.h"
#include "rammel.h"
#include "input.h"
#include "graphics.h"
#include "voxel.h"
#include "timer.h"

// Y is the tunnel depth axis; player is fixed near +Y, obstacles spawn at Y=0
// XZ is the maneuver plane.
#define WORLD_X  VOXELS_X
#define WORLD_Y  VOXELS_Y
#define WORLD_Z  VOXELS_Z
#define CX       ((WORLD_X - 1) * 0.5f)
#define CZ       ((WORLD_Z - 1) * 0.5f)
#define SHIP_Y   ((float)(WORLD_Y - 8))

// cyberpunk neon palette
static const pixel_t neon_palette[] = {
    HEXPIX(00FFFF),  // cyan
    HEXPIX(FF00FF),  // magenta
    HEXPIX(FF0088),  // hot pink
    HEXPIX(8800FF),  // electric purple
    HEXPIX(00FF88),  // acid green
    HEXPIX(FFFF00),  // neon yellow
    HEXPIX(00AAFF),  // ice blue
    HEXPIX(FF5500),  // neon orange
};
#define NUM_NEON (sizeof(neon_palette) / sizeof(neon_palette[0]))

// ── helpers ─────────────────────────────────────────────────────
static inline void set_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x < WORLD_X && (uint32_t)y < WORLD_Y && (uint32_t)z < WORLD_Z) {
        vol[VOXEL_INDEX(x, y, z)] = c;
    }
}

static inline pixel_t fade_colour(pixel_t c, float f) {
    if (f < 0) f = 0; else if (f > 1) f = 1;
    int r = (int)(R_PIX(c) * f);
    int g = (int)(G_PIX(c) * f);
    int b = (int)(B_PIX(c) * f);
    return RGBPIX(r, g, b);
}

static float rand_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static pixel_t random_neon(void) {
    return neon_palette[rand() % NUM_NEON];
}

// ── ship ────────────────────────────────────────────────────────
static float ship_x, ship_z;
static float ship_vx, ship_vz;
static int   ship_lives;
static float ship_invuln;
static bool  ship_alive;
static bool  key_w, key_a, key_s, key_d, key_space;
static bool  boosting;

static void keys_clear(void) {
    key_w = key_a = key_s = key_d = key_space = false;
}

static void ship_init(void) {
    ship_x = CX;
    ship_z = CZ;
    ship_vx = ship_vz = 0;
    ship_invuln = 2.0f;
    ship_alive = true;
}

static void ship_clamp(void) {
    ship_x = clamp(ship_x, 4.0f, WORLD_X - 5.0f);
    ship_z = clamp(ship_z, 3.0f, WORLD_Z - 4.0f);
}

static void ship_update(float dt) {
    if (!ship_alive) return;
    if (ship_invuln > 0) ship_invuln -= dt;

    float ax = 0, az = 0;
    if (key_d) ax += 1;
    if (key_a) ax -= 1;
    if (key_w) az += 1;
    if (key_s) az -= 1;

    ax += input_get_axis(0, AXIS_LS_X);
    az -= input_get_axis(0, AXIS_LS_Y);  // stick forward = up

    const float accel = 320.0f;
    const float damp  = 0.85f;
    ship_vx = (ship_vx + ax * accel * dt) * damp;
    ship_vz = (ship_vz + az * accel * dt) * damp;

    float max_v = 65.0f;
    float v2 = ship_vx*ship_vx + ship_vz*ship_vz;
    if (v2 > max_v*max_v) {
        float s = max_v / sqrtf(v2);
        ship_vx *= s; ship_vz *= s;
    }

    ship_x += ship_vx * dt;
    ship_z += ship_vz * dt;
    ship_clamp();
}

static void ship_draw(pixel_t* vol) {
    if (!ship_alive) return;
    if (ship_invuln > 0 && ((int)(ship_invuln * 10) & 1)) return;

    int sx = (int)ship_x;
    int sy = (int)SHIP_Y;
    int sz = (int)ship_z;

    pixel_t body   = HEXPIX(00FF88);
    pixel_t edge   = HEXPIX(00FFFF);
    pixel_t canopy = HEXPIX(AAFFFF);
    pixel_t glow   = boosting ? HEXPIX(FF00FF) : HEXPIX(FFAA00);

    // arrow nose pointing -Y
    set_voxel(vol, sx, sy - 3, sz, edge);
    set_voxel(vol, sx, sy - 2, sz, body);
    set_voxel(vol, sx - 1, sy - 2, sz, body);
    set_voxel(vol, sx + 1, sy - 2, sz, body);
    // widening hull
    set_voxel(vol, sx - 2, sy - 1, sz, edge);
    set_voxel(vol, sx - 1, sy - 1, sz, body);
    set_voxel(vol, sx,     sy - 1, sz, body);
    set_voxel(vol, sx + 1, sy - 1, sz, body);
    set_voxel(vol, sx + 2, sy - 1, sz, edge);
    // back edge
    set_voxel(vol, sx - 3, sy,     sz, edge);
    set_voxel(vol, sx - 2, sy,     sz, body);
    set_voxel(vol, sx - 1, sy,     sz, body);
    set_voxel(vol, sx,     sy,     sz, body);
    set_voxel(vol, sx + 1, sy,     sz, body);
    set_voxel(vol, sx + 2, sy,     sz, body);
    set_voxel(vol, sx + 3, sy,     sz, edge);
    // cockpit bubble above
    set_voxel(vol, sx, sy - 1, sz + 1, canopy);
    set_voxel(vol, sx, sy,     sz + 1, canopy);
    // underside fins
    set_voxel(vol, sx - 2, sy, sz - 1, edge);
    set_voxel(vol, sx + 2, sy, sz - 1, edge);
    // engine plume
    set_voxel(vol, sx - 1, sy + 1, sz, glow);
    set_voxel(vol, sx,     sy + 1, sz, glow);
    set_voxel(vol, sx + 1, sy + 1, sz, glow);
    if (boosting) {
        set_voxel(vol, sx,     sy + 2, sz, HEXPIX(FFFFFF));
        set_voxel(vol, sx - 1, sy + 2, sz, HEXPIX(FF55FF));
        set_voxel(vol, sx + 1, sy + 2, sz, HEXPIX(FF55FF));
        set_voxel(vol, sx,     sy + 3, sz, HEXPIX(FF0088));
    } else if ((rand() & 3) == 0) {
        set_voxel(vol, sx, sy + 2, sz, HEXPIX(FF5500));
    }
}

// ── obstacles ───────────────────────────────────────────────────
enum {
    SHAPE_RING,
    SHAPE_DUAL_RING,
    SHAPE_PLUS,
    SHAPE_DIAMOND,
    SHAPE_SLATS,
    SHAPE_IRIS,
    SHAPE_LASER,
    SHAPE_COUNT
};

typedef struct {
    int     type;
    float   y;
    float   rotation;
    float   rot_speed;
    float   phase;
    float   phase_speed;
    pixel_t primary, accent;
    int     seed;
    bool    active;
    bool    scored;
} obstacle_t;

#define MAX_OBSTACLES 10
static obstacle_t obstacles[MAX_OBSTACLES];

// ── per-shape wall tests ───────────────────────────────────────
// returns 0 = open (gap / outside), 1 = primary wall, 2 = accent wall

static int shape_ring(obstacle_t* o, int x, int z) {
    float dx = x - CX, dz = z - CZ;
    float r2 = dx*dx + dz*dz;
    const float r_in = 20.0f, r_out = 24.0f;
    if (r2 < r_in*r_in || r2 > r_out*r_out) return 0;
    float angle = atan2f(dz, dx);
    float diff = angle - o->rotation;
    while (diff >  M_PI) diff -= 2 * M_PI;
    while (diff < -M_PI) diff += 2 * M_PI;
    if (fabsf(diff) < 0.7f) return 0;  // gap
    int sector = (int)((angle + M_PI) * 3.0f);  // ~12 sectors
    return (sector & 1) ? 1 : 2;
}

static int shape_dual_ring(obstacle_t* o, int x, int z) {
    float dx = x - CX, dz = z - CZ;
    float r  = sqrtf(dx*dx + dz*dz);
    float angle = atan2f(dz, dx);

    // outer ring at r=22, rotating clockwise
    if (r > 20.5f && r < 23.5f) {
        float diff = angle - o->rotation;
        while (diff >  M_PI) diff -= 2 * M_PI;
        while (diff < -M_PI) diff += 2 * M_PI;
        if (fabsf(diff) < 0.9f) return 0;
        return 1;
    }
    // inner ring at r=11, counter-rotating
    if (r > 9.5f && r < 12.5f) {
        float diff = angle - (-o->rotation * 1.5f);
        while (diff >  M_PI) diff -= 2 * M_PI;
        while (diff < -M_PI) diff += 2 * M_PI;
        if (fabsf(diff) < 1.0f) return 0;
        return 2;
    }
    return 0;
}

static int shape_plus(obstacle_t* o, int x, int z) {
    float dx = x - CX, dz = z - CZ;
    float cs = cosf(-o->rotation), sn = sinf(-o->rotation);
    float rx = dx * cs - dz * sn;
    float rz = dx * sn + dz * cs;
    const float outer = 24.0f;
    const float corridor = 6.0f;
    if (fabsf(rx) > outer || fabsf(rz) > outer) return 0;
    if (fabsf(rx) < corridor || fabsf(rz) < corridor) return 0;
    // four quadrants alternate colour
    bool pos_x = rx > 0, pos_z = rz > 0;
    return (pos_x ^ pos_z) ? 1 : 2;
}

static int shape_diamond(obstacle_t* o, int x, int z) {
    float dx = x - CX, dz = z - CZ;
    float cs = cosf(-o->rotation), sn = sinf(-o->rotation);
    float rx = dx * cs - dz * sn;
    float rz = dx * sn + dz * cs;
    const float R = 22.0f;
    const float t = 1.6f;
    // rotating square frame
    bool on_frame =
        (fabsf(fabsf(rx) - R) < t && fabsf(rz) <= R + t) ||
        (fabsf(fabsf(rz) - R) < t && fabsf(rx) <= R + t);
    if (!on_frame) return 0;
    // gap on one side (rotates with the frame): on +rx edge, middle
    if (rx > R - t && fabsf(rz) < R * 0.35f) return 0;
    // corners = accent
    if (fabsf(rx) > R - 3.0f && fabsf(rz) > R - 3.0f) return 2;
    return 1;
}

static int shape_slats(obstacle_t* o, int x, int z) {
    (void)x;
    const int n = 5;
    const float span = WORLD_Z - 8.0f;
    float spacing = span / n;
    int missing = o->seed % n;
    int shift = (int)(o->phase * 2.0f);  // slowly slides
    missing = (missing + shift) % n;
    for (int k = 0; k < n; ++k) {
        float bz = 4.0f + spacing * (k + 0.5f);
        if (fabsf(z - bz) < 1.8f) {
            if (k == missing) return 0;   // gap
            return (k & 1) ? 1 : 2;
        }
    }
    return 0;
}

static int shape_iris(obstacle_t* o, int x, int z) {
    float dx = x - CX, dz = z - CZ;
    float r = sqrtf(dx*dx + dz*dz);
    float iris_r = 5.0f + (0.5f + 0.5f * sinf(o->phase)) * 14.0f;
    float outer_r = 26.0f;
    if (r < iris_r || r > outer_r) return 0;
    // concentric colour bands
    int band = (int)((r - iris_r) * 0.5f);
    return (band & 1) ? 1 : 2;
}

static int shape_laser(obstacle_t* o, int x, int z) {
    float lx = CX + sinf(o->phase)        * 30.0f;
    float lz = CZ + cosf(o->phase * 0.7f) * 18.0f;
    bool on_x = fabsf(x - lx) < 1.5f;
    bool on_z = fabsf(z - lz) < 1.5f;
    if (on_x && on_z) return 1;           // intersection glow
    if (on_x) return 1;
    if (on_z) return 2;
    return 0;
}

static int obstacle_which(obstacle_t* o, int x, int z) {
    switch (o->type) {
    case SHAPE_RING:      return shape_ring(o, x, z);
    case SHAPE_DUAL_RING: return shape_dual_ring(o, x, z);
    case SHAPE_PLUS:      return shape_plus(o, x, z);
    case SHAPE_DIAMOND:   return shape_diamond(o, x, z);
    case SHAPE_SLATS:     return shape_slats(o, x, z);
    case SHAPE_IRIS:      return shape_iris(o, x, z);
    case SHAPE_LASER:     return shape_laser(o, x, z);
    }
    return 0;
}

// ── obstacle spawn / update / draw ──────────────────────────────
static float tunnel_speed    = 45.0f;
static float spawn_timer     = 0;
static float spawn_interval  = 1.6f;
static int   score           = 0;
static float distance        = 0;

static void obstacles_init(void) {
    memset(obstacles, 0, sizeof(obstacles));
    tunnel_speed   = 45.0f;
    spawn_timer    = 0.5f;
    spawn_interval = 1.6f;
    score          = 0;
    distance       = 0;
}

static void obstacle_spawn(void) {
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if (!obstacles[i].active) {
            obstacle_t* o = &obstacles[i];
            o->type = rand() % SHAPE_COUNT;
            o->y = 0;
            o->rotation = rand_f(0, (float)(2 * M_PI));
            o->rot_speed = rand_f(-1.2f, 1.2f);
            o->phase = rand_f(0, (float)(2 * M_PI));
            o->phase_speed = rand_f(1.2f, 2.8f);
            o->seed = rand();
            o->primary = random_neon();
            o->accent = random_neon();
            while (o->accent == o->primary) o->accent = random_neon();
            o->active = true;
            o->scored = false;
            return;
        }
    }
}

static void spawn_explosion(float x, float y, float z, pixel_t c, int count);

static void obstacles_update(float dt) {
    // global speed ramps with score
    tunnel_speed = 45.0f + (float)score * 0.6f;
    if (tunnel_speed > 110.0f) tunnel_speed = 110.0f;
    spawn_interval = 1.6f - (float)score * 0.012f;
    if (spawn_interval < 0.55f) spawn_interval = 0.55f;

    spawn_timer -= dt;
    if (spawn_timer <= 0) {
        obstacle_spawn();
        spawn_timer = spawn_interval;
    }

    distance += tunnel_speed * dt;

    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        obstacle_t* o = &obstacles[i];
        if (!o->active) continue;

        o->y        += tunnel_speed * dt;
        o->rotation += o->rot_speed * dt;
        o->phase    += o->phase_speed * dt;

        if (o->y > WORLD_Y + 3) { o->active = false; continue; }

        // collision window when obstacle plane is near ship
        if (!o->scored && ship_alive && ship_invuln <= 0 &&
            fabsf(o->y - SHIP_Y) < 1.6f) {
            if (obstacle_which(o, (int)ship_x, (int)ship_z) != 0) {
                spawn_explosion(ship_x, SHIP_Y, ship_z, o->primary, 30);
                spawn_explosion(ship_x, SHIP_Y, ship_z, HEXPIX(FFFFFF), 15);
                o->scored = true;
                ship_lives--;
                if (ship_lives <= 0) {
                    ship_alive = false;
                    printf("wreck! score: %d  distance: %.0f\n", score, distance);
                } else {
                    ship_invuln = 2.0f;
                    ship_vx = ship_vz = 0;
                }
            }
        }

        // past the ship: award point
        if (!o->scored && o->y > SHIP_Y + 1.2f) {
            o->scored = true;
            score++;
        }
    }
}

static void obstacles_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        obstacle_t* o = &obstacles[i];
        if (!o->active) continue;

        int y_int = (int)o->y;
        if (y_int < 0 || y_int >= WORLD_Y) continue;

        // distance fade: far away = dim, close = bright
        float d = SHIP_Y - o->y;
        if (d < 0) d = 0;
        float fade = 1.0f - (d / SHIP_Y) * 0.75f;
        if (fade < 0.25f) fade = 0.25f;

        pixel_t p = fade_colour(o->primary, fade);
        pixel_t a = fade_colour(o->accent,  fade);

        // render 2 Y-layers for volumetric thickness
        for (int z = 0; z < WORLD_Z; ++z) {
            for (int x = 0; x < WORLD_X; ++x) {
                int w = obstacle_which(o, x, z);
                if (w == 0) continue;
                pixel_t c = (w == 1) ? p : a;
                set_voxel(vol, x, y_int,     z, c);
                set_voxel(vol, x, y_int + 1, z, c);
            }
        }
    }
}

// ── warp-star background ────────────────────────────────────────
#define NUM_STARS 180

typedef struct {
    float x, y, z;
    float speed_mul;  // parallax
} star_t;

static star_t stars[NUM_STARS];

static void stars_init(void) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].x = rand_f(0, WORLD_X);
        stars[i].y = rand_f(0, WORLD_Y);
        stars[i].z = rand_f(0, WORLD_Z);
        stars[i].speed_mul = rand_f(0.5f, 1.3f);
    }
}

static void stars_update(float dt) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].y += tunnel_speed * dt * stars[i].speed_mul * 0.6f;
        if (stars[i].y >= WORLD_Y) {
            stars[i].y = 0;
            stars[i].x = rand_f(0, WORLD_X);
            stars[i].z = rand_f(0, WORLD_Z);
        }
    }
}

static void stars_draw(pixel_t* vol) {
    for (int i = 0; i < NUM_STARS; ++i) {
        int sx = (int)stars[i].x;
        int sy = (int)stars[i].y;
        int sz = (int)stars[i].z;

        pixel_t c = HEXPIX(333355);
        if (stars[i].speed_mul > 1.0f) c = HEXPIX(5577AA);

        set_voxel(vol, sx, sy, sz, c);

        // streak when boosting
        if (boosting) {
            set_voxel(vol, sx, sy - 1, sz, HEXPIX(FF55FF));
            set_voxel(vol, sx, sy - 2, sz, HEXPIX(880088));
        }
    }
}

// ── corner rails (depth cue) ────────────────────────────────────
static float rail_scroll = 0;

static void rails_update(float dt) {
    rail_scroll += tunnel_speed * dt * 0.5f;
}

static void rails_draw(pixel_t* vol) {
    int spacing = 10;
    int offset = ((int)rail_scroll) % spacing;
    pixel_t base = HEXPIX(00AAFF);

    for (int k = 0; k < WORLD_Y / spacing + 2; ++k) {
        int y = k * spacing - offset;
        if (y < 0 || y >= WORLD_Y) continue;

        float fade = 0.2f + (float)y / WORLD_Y * 0.8f;
        pixel_t c = fade_colour(base, fade);

        set_voxel(vol, 2,           y, 2,           c);
        set_voxel(vol, WORLD_X - 3, y, 2,           c);
        set_voxel(vol, 2,           y, WORLD_Z - 3, c);
        set_voxel(vol, WORLD_X - 3, y, WORLD_Z - 3, c);
    }
}

// ── particles (wreck sparks) ────────────────────────────────────
#define MAX_PARTICLES 384

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    pixel_t colour;
    bool active;
} particle_t;

static particle_t particles[MAX_PARTICLES];

static void particles_init(void) {
    memset(particles, 0, sizeof(particles));
}

static void spawn_explosion(float x, float y, float z, pixel_t c, int n) {
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!particles[i].active) {
                particle_t* p = &particles[i];
                p->active = true;
                p->x = x; p->y = y; p->z = z;
                p->vx = rand_f(-50, 50);
                p->vy = rand_f(-50, 50);
                p->vz = rand_f(-50, 50);
                p->life = rand_f(0.3f, 1.0f);
                p->colour = c;
                break;
            }
        }
    }
}

static void particles_update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;
        p->x += p->vx * dt;
        p->y += (p->vy + tunnel_speed * 0.3f) * dt;
        p->z += p->vz * dt;
        p->vx *= 0.96f; p->vy *= 0.96f; p->vz *= 0.96f;
        p->life -= dt;
        if (p->life <= 0) p->active = false;
    }
}

static void particles_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particle_t* p = &particles[i];
        if (!p->active) continue;
        pixel_t c = p->colour;
        if (p->life < 0.3f) c = fade_colour(c, p->life / 0.3f);
        set_voxel(vol, (int)p->x, (int)p->y, (int)p->z, c);
    }
}

// ── HUD ─────────────────────────────────────────────────────────
static void hud_draw(pixel_t* vol) {
    // lives: red squares along bottom
    for (int i = 0; i < ship_lives && i < 5; ++i) {
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, 2, HEXPIX(FF0055));
        set_voxel(vol, 4 + i * 3, WORLD_Y - 3, 2, HEXPIX(FF0055));
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, 3, HEXPIX(FF0055));
        set_voxel(vol, 4 + i * 3, WORLD_Y - 3, 3, HEXPIX(FF0055));
    }

    // boost-charge indicator (top): green bar while boosting
    if (boosting) {
        for (int i = 0; i < 10; ++i) {
            set_voxel(vol, WORLD_X - 4 - i, WORLD_Y - 3, WORLD_Z - 3, HEXPIX(00FF88));
        }
    }
}

// ── main ────────────────────────────────────────────────────────
static float gameover_timer = 0;

static void game_reset(void) {
    ship_init();
    ship_lives = 3;
    obstacles_init();
    particles_init();
    stars_init();
    keys_clear();
    rail_scroll = 0;
    gameover_timer = 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do { sleep(1); } while (!voxel_buffer_map());
    }

    timer_init();
    game_reset();

    printf("=== NEON TUNNEL ===\n");
    printf("  WASD  dodge (W/S = up/down, A/D = left/right)\n");
    printf("  SPACE boost\n");
    printf("  R     restart\n");
    printf("  ESC   quit\n\n");

    input_set_nonblocking();

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);

        switch (ch) {
            case 'w': key_w = true; break;
            case 'a': key_a = true; break;
            case 's': key_s = true; break;
            case 'd': key_d = true; break;
            case ' ': key_space = true; break;
            case 'r':
            case 'R':
                game_reset();
                printf("restart! score: 0\n");
                break;
        }

        input_update();

        boosting = ship_alive &&
                   (key_space ||
                    input_get_button(0, BUTTON_RB, BUTTON_HELD) ||
                    input_get_button(0, BUTTON_A,  BUTTON_HELD));

        float dt_game = boosting ? dt * 1.8f : dt;

        ship_update(dt);
        obstacles_update(dt_game);
        particles_update(dt);
        stars_update(dt_game);
        rails_update(dt_game);

        if (!ship_alive) {
            gameover_timer += dt;
            if (gameover_timer > 5.0f || input_get_button(0, BUTTON_A, BUTTON_PRESSED)) {
                game_reset();
            }
        }

        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        stars_draw(vol);
        rails_draw(vol);
        obstacles_draw(vol);
        ship_draw(vol);
        particles_draw(vol);
        hud_draw(vol);

        voxel_buffer_swap();

        keys_clear();
        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();
    return 0;
}
