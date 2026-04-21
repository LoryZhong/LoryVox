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

// world bounds
#define WORLD_X  VOXELS_X
#define WORLD_Y  VOXELS_Y
#define WORLD_Z  VOXELS_Z
#define CX       ((WORLD_X - 1) * 0.5f)
#define CY       ((WORLD_Y - 1) * 0.5f)
#define CZ       ((WORLD_Z - 1) * 0.5f)

// weapon types
enum {
    WEAPON_SINGLE = 0,
    WEAPON_TRIPLE,
    WEAPON_LASER,
    WEAPON_RAPID,
    WEAPON_COUNT
};

static const float weapon_cooldown[WEAPON_COUNT] = {
    0.12f,  // single
    0.18f,  // triple spread
    0.25f,  // laser (continuous beam drawn for 0.1s)
    0.05f,  // rapid
};

// keyboard state
static bool key_w, key_a, key_s, key_d, key_z, key_x, key_space;

static void keys_clear(void) {
    key_w = key_a = key_s = key_d = key_z = key_x = key_space = false;
}

// ship
static float ship_x, ship_y, ship_z;
static float ship_vx, ship_vy, ship_vz;
static const float ship_accel    = 300.0f;
static const float ship_damping  = 0.88f;
static const float ship_max_speed = 70.0f;
static int  ship_lives;
static float ship_invuln;
static bool ship_alive;
static int  ship_weapon;
static float weapon_timer;   // remaining time on current pickup (WEAPON_SINGLE = infinite)

static inline void set_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x < WORLD_X && (uint32_t)y < WORLD_Y && (uint32_t)z < WORLD_Z) {
        vol[VOXEL_INDEX(x, y, z)] = c;
    }
}

static void ship_init(void) {
    ship_x = CX;
    ship_y = WORLD_Y * 0.75f;  // start near the bottom (player side)
    ship_z = CZ;
    ship_vx = ship_vy = ship_vz = 0;
    ship_alive = true;
    ship_invuln = 2.0f;
    ship_weapon = WEAPON_SINGLE;
    weapon_timer = 0;
}

static void ship_clamp(void) {
    ship_x = clamp(ship_x, 4.0f, WORLD_X - 5.0f);
    ship_y = clamp(ship_y, 4.0f, WORLD_Y - 5.0f);
    ship_z = clamp(ship_z, 3.0f, WORLD_Z - 4.0f);
}

static void ship_update(float dt) {
    if (!ship_alive) return;
    if (ship_invuln > 0) ship_invuln -= dt;

    if (ship_weapon != WEAPON_SINGLE) {
        weapon_timer -= dt;
        if (weapon_timer <= 0) {
            ship_weapon = WEAPON_SINGLE;
        }
    }

    float ax = 0, ay = 0, az = 0;

    if (key_d)  ax += ship_accel;
    if (key_a)  ax -= ship_accel;
    if (key_w)  ay -= ship_accel;
    if (key_s)  ay += ship_accel;
    if (key_x)  az += ship_accel;
    if (key_z)  az -= ship_accel;

    ax += input_get_axis(0, AXIS_LS_X) * ship_accel;
    ay += input_get_axis(0, AXIS_LS_Y) * ship_accel;
    az += (input_get_axis(0, AXIS_RT) - input_get_axis(0, AXIS_LT)) * ship_accel;

    ship_vx = (ship_vx + ax * dt) * ship_damping;
    ship_vy = (ship_vy + ay * dt) * ship_damping;
    ship_vz = (ship_vz + az * dt) * ship_damping;

    float spd = sqrtf(ship_vx*ship_vx + ship_vy*ship_vy + ship_vz*ship_vz);
    if (spd > ship_max_speed) {
        float s = ship_max_speed / spd;
        ship_vx *= s; ship_vy *= s; ship_vz *= s;
    }

    ship_x += ship_vx * dt;
    ship_y += ship_vy * dt;
    ship_z += ship_vz * dt;
    ship_clamp();
}

// 3D volumetric ship, nose pointing -Y
static void ship_draw(pixel_t* vol) {
    if (!ship_alive) return;
    if (ship_invuln > 0 && ((int)(ship_invuln * 10) & 1)) return;

    int sx = (int)ship_x;
    int sy = (int)ship_y;
    int sz = (int)ship_z;

    pixel_t body    = HEXPIX(00FF55);
    pixel_t nose    = HEXPIX(FFFFAA);
    pixel_t wing    = HEXPIX(0077FF);
    pixel_t cockpit = HEXPIX(AAEEFF);
    pixel_t fin     = HEXPIX(FF5500);
    pixel_t engine  = HEXPIX(00CC33);

    // when a pickup is active, tint the body to hint at the active weapon
    if (ship_weapon == WEAPON_TRIPLE)       body = HEXPIX(FFAA00);
    else if (ship_weapon == WEAPON_LASER)   body = HEXPIX(FF00FF);
    else if (ship_weapon == WEAPON_RAPID)   body = HEXPIX(FFFF00);

    // fuselage (z = sz, along Y)
    set_voxel(vol, sx, sy - 3, sz, nose);
    set_voxel(vol, sx, sy - 2, sz, body);
    set_voxel(vol, sx, sy - 1, sz, body);
    set_voxel(vol, sx, sy,     sz, body);
    set_voxel(vol, sx, sy + 1, sz, body);
    set_voxel(vol, sx, sy + 2, sz, engine);

    // cockpit bubble (above, z+1)
    set_voxel(vol, sx, sy - 1, sz + 1, cockpit);
    set_voxel(vol, sx, sy,     sz + 1, cockpit);

    // main wings (z = sz) spread along X at sy, swept back
    set_voxel(vol, sx - 1, sy,     sz, wing);
    set_voxel(vol, sx - 2, sy,     sz, wing);
    set_voxel(vol, sx - 3, sy + 1, sz, wing);
    set_voxel(vol, sx + 1, sy,     sz, wing);
    set_voxel(vol, sx + 2, sy,     sz, wing);
    set_voxel(vol, sx + 3, sy + 1, sz, wing);

    // wing-tip dihedral (droop below)
    set_voxel(vol, sx - 3, sy + 1, sz - 1, wing);
    set_voxel(vol, sx + 3, sy + 1, sz - 1, wing);

    // vertical tail fin (goes up in Z at the back)
    set_voxel(vol, sx, sy + 1, sz + 1, fin);
    set_voxel(vol, sx, sy + 2, sz + 1, fin);
    set_voxel(vol, sx, sy + 2, sz + 2, fin);

    // thruster plume
    bool thrusting = key_w || input_get_axis(0, AXIS_LS_Y) < -0.1f;
    if (thrusting) {
        pixel_t flame1 = HEXPIX(FFAA00);
        pixel_t flame2 = HEXPIX(FF3300);
        set_voxel(vol, sx,     sy + 3, sz, flame1);
        set_voxel(vol, sx - 1, sy + 3, sz, flame1);
        set_voxel(vol, sx + 1, sy + 3, sz, flame1);
        if (rand() & 1) {
            set_voxel(vol, sx, sy + 4, sz, flame2);
        }
    }
}

// bullets (player)
#define MAX_BULLETS 96

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    bool active;
    pixel_t colour;
    bool piercing;
} bullet_t;

static bullet_t bullets[MAX_BULLETS];

static const float bullet_speed = 140.0f;
static const float bullet_life  = 1.5f;
static float fire_cooldown = 0;

static void bullets_init(void) {
    memset(bullets, 0, sizeof(bullets));
}

static void bullet_add(float x, float y, float z, float vx, float vy, float vz,
                       pixel_t c, bool piercing) {
    for (int i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i] = (bullet_t){
                .x = x, .y = y, .z = z,
                .vx = vx, .vy = vy, .vz = vz,
                .life = bullet_life,
                .active = true,
                .colour = c,
                .piercing = piercing,
            };
            return;
        }
    }
}

static void weapon_fire(void) {
    switch (ship_weapon) {
    case WEAPON_SINGLE:
        bullet_add(ship_x, ship_y - 4, ship_z, 0, -bullet_speed, 0,
                   HEXPIX(FFFF55), false);
        break;
    case WEAPON_TRIPLE: {
        float spread = 28.0f;
        bullet_add(ship_x,     ship_y - 4, ship_z, 0,       -bullet_speed, 0,
                   HEXPIX(FFAA00), false);
        bullet_add(ship_x - 1, ship_y - 3, ship_z, -spread, -bullet_speed * 0.95f, 0,
                   HEXPIX(FFAA00), false);
        bullet_add(ship_x + 1, ship_y - 3, ship_z,  spread, -bullet_speed * 0.95f, 0,
                   HEXPIX(FFAA00), false);
        break;
    }
    case WEAPON_LASER:
        // piercing, fast, goes through multiple enemies
        bullet_add(ship_x, ship_y - 4, ship_z, 0, -bullet_speed * 2.0f, 0,
                   HEXPIX(FF55FF), true);
        bullet_add(ship_x, ship_y - 5, ship_z, 0, -bullet_speed * 2.0f, 0,
                   HEXPIX(FFAAFF), true);
        break;
    case WEAPON_RAPID:
        bullet_add(ship_x - 2, ship_y - 3, ship_z, 0, -bullet_speed, 0,
                   HEXPIX(FFFF00), false);
        bullet_add(ship_x + 2, ship_y - 3, ship_z, 0, -bullet_speed, 0,
                   HEXPIX(FFFF00), false);
        break;
    }
}

static void bullets_update(float dt) {
    if (fire_cooldown > 0) fire_cooldown -= dt;

    bool firing = key_space || input_get_button(0, BUTTON_RB, BUTTON_HELD)
                            || input_get_button(0, BUTTON_A, BUTTON_HELD);

    if (firing && fire_cooldown <= 0 && ship_alive) {
        weapon_fire();
        fire_cooldown = weapon_cooldown[ship_weapon];
    }

    for (int i = 0; i < MAX_BULLETS; ++i) {
        bullet_t* b = &bullets[i];
        if (!b->active) continue;

        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->z += b->vz * dt;
        b->life -= dt;

        if (b->life <= 0 || b->y < 0 || b->y >= WORLD_Y ||
            b->x < 0 || b->x >= WORLD_X ||
            b->z < 0 || b->z >= WORLD_Z) {
            b->active = false;
        }
    }
}

static void bullets_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_BULLETS; ++i) {
        bullet_t* b = &bullets[i];
        if (!b->active) continue;
        int bx = (int)b->x, by = (int)b->y, bz = (int)b->z;

        set_voxel(vol, bx, by, bz, b->colour);
        if (b->piercing) {
            // laser is a short trail
            set_voxel(vol, bx, by + 1, bz, b->colour);
            set_voxel(vol, bx, by + 2, bz, HEXPIX(FFAAFF));
        } else {
            set_voxel(vol, bx, by + 1, bz, HEXPIX(FF8800));
        }
    }
}

// enemy bullets
#define MAX_EBULLETS 64

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    bool active;
} ebullet_t;

static ebullet_t ebullets[MAX_EBULLETS];

static void ebullets_init(void) {
    memset(ebullets, 0, sizeof(ebullets));
}

static void ebullet_spawn(float x, float y, float z, float tx, float ty, float tz) {
    float dx = tx - x, dy = ty - y, dz = tz - z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return;
    float s = 55.0f / len;

    for (int i = 0; i < MAX_EBULLETS; ++i) {
        if (!ebullets[i].active) {
            ebullets[i] = (ebullet_t){
                .x = x, .y = y, .z = z,
                .vx = dx * s, .vy = dy * s, .vz = dz * s,
                .life = 3.0f, .active = true,
            };
            return;
        }
    }
}

static void ebullets_update(float dt) {
    for (int i = 0; i < MAX_EBULLETS; ++i) {
        ebullet_t* b = &ebullets[i];
        if (!b->active) continue;

        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->z += b->vz * dt;
        b->life -= dt;

        if (b->life <= 0 ||
            b->x < 0 || b->x >= WORLD_X ||
            b->y < 0 || b->y >= WORLD_Y ||
            b->z < 0 || b->z >= WORLD_Z) {
            b->active = false;
        }
    }
}

static void ebullets_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_EBULLETS; ++i) {
        ebullet_t* b = &ebullets[i];
        if (!b->active) continue;
        int bx = (int)b->x, by = (int)b->y, bz = (int)b->z;
        set_voxel(vol, bx, by, bz, HEXPIX(FF3333));
        set_voxel(vol, bx, by - 1, bz, HEXPIX(AA0000));
    }
}

// enemies (Space Invaders style formation + dive-bombers)
#define ROWS_MAX 4
#define COLS_MAX 7
#define MAX_ENEMIES (ROWS_MAX * COLS_MAX)

typedef struct {
    float x, y, z;
    int   row, col;       // position in formation
    int   tier;           // 0..2: grunt / mid / elite
    int   hp;
    bool  active;
    bool  diving;         // broke formation and is diving at player
    float dive_vx, dive_vy, dive_vz;
    float fire_timer;
} enemy_t;

static enemy_t enemies[MAX_ENEMIES];
static float formation_x = 0;          // formation origin X offset
static float formation_y = 0;          // formation origin Y offset
static int   formation_dir = 1;        // +1 right, -1 left
static float formation_speed = 8.0f;
static int   wave = 1;
static int   score = 0;

static float rand_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static void enemies_init_wave(int w) {
    memset(enemies, 0, sizeof(enemies));
    formation_x = 0;
    formation_y = 0;
    formation_dir = 1;
    formation_speed = 8.0f + (w - 1) * 1.5f;

    int rows = 3 + (w > 2 ? 1 : 0);
    int cols = 5 + (w > 1 ? 2 : 0);
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    if (cols > COLS_MAX) cols = COLS_MAX;

    float spacing_x = 10.0f;
    float spacing_y = 8.0f;
    float base_x = CX - (cols - 1) * spacing_x * 0.5f;
    float base_y = 12.0f;

    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        int tier = (r == 0) ? 2 : (r == 1 ? 1 : 0);  // top row = elite
        for (int c = 0; c < cols; ++c) {
            enemy_t* e = &enemies[idx++];
            e->active = true;
            e->row = r; e->col = c;
            e->tier = tier;
            e->hp = 1 + tier;
            e->x = base_x + c * spacing_x;
            e->y = base_y + r * spacing_y;
            e->z = CZ + sinf((float)c * 0.9f) * 4.0f;  // gentle Z wave
            e->fire_timer = rand_f(2.0f, 6.0f);
        }
    }
}

static int enemies_alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; ++i) if (enemies[i].active) ++n;
    return n;
}

static void enemy_draw_one(pixel_t* vol, enemy_t* e) {
    int ex = (int)e->x, ey = (int)e->y, ez = (int)e->z;

    pixel_t body, accent, glow;
    switch (e->tier) {
    case 0:  // grunt: teal
        body   = HEXPIX(00AAAA);
        accent = HEXPIX(55EEEE);
        glow   = HEXPIX(FF00AA);
        break;
    case 1:  // mid: purple
        body   = HEXPIX(AA22FF);
        accent = HEXPIX(FF88FF);
        glow   = HEXPIX(FFAA00);
        break;
    default: // elite: red/gold
        body   = HEXPIX(EE2222);
        accent = HEXPIX(FFCC00);
        glow   = HEXPIX(FFFFFF);
        break;
    }

    if (e->tier == 0) {
        // small saucer, 5 voxels
        set_voxel(vol, ex - 1, ey, ez, body);
        set_voxel(vol, ex,     ey, ez, body);
        set_voxel(vol, ex + 1, ey, ez, body);
        set_voxel(vol, ex, ey + 1, ez, accent);  // nose toward player (+Y)
        set_voxel(vol, ex, ey, ez + 1, glow);
    } else if (e->tier == 1) {
        // wider fighter with wings
        set_voxel(vol, ex,     ey - 1, ez, body);
        set_voxel(vol, ex,     ey,     ez, body);
        set_voxel(vol, ex,     ey + 1, ez, accent);
        set_voxel(vol, ex - 1, ey,     ez, body);
        set_voxel(vol, ex + 1, ey,     ez, body);
        set_voxel(vol, ex - 2, ey,     ez, accent);
        set_voxel(vol, ex + 2, ey,     ez, accent);
        set_voxel(vol, ex, ey, ez + 1, glow);
    } else {
        // elite: big cross-shape with Z-wings
        set_voxel(vol, ex, ey - 1, ez, body);
        set_voxel(vol, ex, ey,     ez, body);
        set_voxel(vol, ex, ey + 1, ez, body);
        set_voxel(vol, ex, ey + 2, ez, accent);  // pointed nose
        set_voxel(vol, ex - 1, ey, ez, body);
        set_voxel(vol, ex + 1, ey, ez, body);
        set_voxel(vol, ex - 2, ey, ez, accent);
        set_voxel(vol, ex + 2, ey, ez, accent);
        set_voxel(vol, ex, ey, ez - 1, body);
        set_voxel(vol, ex, ey, ez + 1, body);
        set_voxel(vol, ex, ey, ez + 2, glow);     // top glow
        set_voxel(vol, ex, ey, ez - 2, glow);
    }
}

static void spawn_explosion(float x, float y, float z, pixel_t c, int count);
static void spawn_pickup(float x, float y, float z);

static void enemies_update(float dt) {
    // step formation: move left/right, bounce off walls by descending
    float lx = 1e9f, rx = -1e9f;
    int any_active = 0;
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        enemy_t* e = &enemies[i];
        if (!e->active || e->diving) continue;
        if (e->x < lx) lx = e->x;
        if (e->x > rx) rx = e->x;
        ++any_active;
    }

    formation_x += formation_dir * formation_speed * dt;
    bool bounced = false;
    if (any_active && (rx + formation_x > WORLD_X - 4)) {
        formation_dir = -1;
        bounced = true;
    } else if (any_active && (lx + formation_x < 4)) {
        formation_dir = 1;
        bounced = true;
    }
    if (bounced) {
        formation_y += 4.0f;  // step toward the player
    }

    // occasionally promote a random enemy to "diver"
    static float dive_timer = 0;
    dive_timer -= dt;
    if (dive_timer <= 0) {
        dive_timer = rand_f(2.5f, 5.0f) - (wave - 1) * 0.3f;
        if (dive_timer < 0.8f) dive_timer = 0.8f;

        int candidates[MAX_ENEMIES], nc = 0;
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            if (enemies[i].active && !enemies[i].diving) candidates[nc++] = i;
        }
        if (nc > 0 && ship_alive) {
            enemy_t* e = &enemies[candidates[rand() % nc]];
            // freeze current absolute position before diving
            e->x += formation_x;
            e->y += formation_y;
            e->diving = true;
            float dx = ship_x - e->x;
            float dy = ship_y - e->y;
            float dz = ship_z - e->z;
            float len = sqrtf(dx*dx + dy*dy + dz*dz);
            float spd = 30.0f + wave * 2.0f;
            if (len > 0.001f) {
                e->dive_vx = dx / len * spd;
                e->dive_vy = dy / len * spd;
                e->dive_vz = dz / len * spd;
            }
        }
    }

    // update and handle collisions / firing
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        enemy_t* e = &enemies[i];
        if (!e->active) continue;

        if (e->diving) {
            e->x += e->dive_vx * dt;
            e->y += e->dive_vy * dt;
            e->z += e->dive_vz * dt;

            // out of world? deactivate
            if (e->y >= WORLD_Y - 1 || e->x < 1 || e->x >= WORLD_X - 1 ||
                e->z < 1 || e->z >= WORLD_Z - 1) {
                e->active = false;
                continue;
            }
        }
        // formation members follow formation offset
        // (for static formation members, their x/y stays as base + formation_x/formation_y)

        // firing
        e->fire_timer -= dt;
        if (e->fire_timer <= 0 && ship_alive) {
            e->fire_timer = rand_f(3.0f, 7.0f) - (wave - 1) * 0.2f;
            if (e->fire_timer < 0.5f) e->fire_timer = 0.5f;

            float ex = e->diving ? e->x : e->x + formation_x;
            float ey = e->diving ? e->y : e->y + formation_y;
            ebullet_spawn(ex, ey + 2, e->z, ship_x, ship_y, ship_z);
        }

        // player-bullet collision
        float bx_off = e->diving ? 0 : formation_x;
        float by_off = e->diving ? 0 : formation_y;
        float ex = e->x + bx_off;
        float ey = e->y + by_off;

        float hit_r = 2.5f + e->tier * 0.5f;
        for (int j = 0; j < MAX_BULLETS; ++j) {
            bullet_t* b = &bullets[j];
            if (!b->active) continue;
            float dx = b->x - ex, dy = b->y - ey, dz = b->z - e->z;
            if (dx*dx + dy*dy + dz*dz < hit_r * hit_r) {
                if (!b->piercing) b->active = false;
                e->hp--;
                spawn_explosion(b->x, b->y, b->z, HEXPIX(FFFFFF), 3);
                if (e->hp <= 0) {
                    pixel_t boom = (e->tier == 0) ? HEXPIX(00FFFF)
                                  : (e->tier == 1) ? HEXPIX(FF88FF)
                                                   : HEXPIX(FFAA00);
                    spawn_explosion(ex, ey, e->z, boom, 20 + e->tier * 10);
                    score += (e->tier + 1) * 20;

                    // elite kills have a chance to drop a pickup
                    int drop_chance = (e->tier == 2) ? 100 : (e->tier == 1 ? 30 : 8);
                    if ((rand() % 100) < drop_chance) {
                        spawn_pickup(ex, ey, e->z);
                    }

                    e->active = false;
                    break;
                }
            }
        }

        if (!e->active) continue;

        // ship-enemy collision
        if (ship_alive && ship_invuln <= 0) {
            float dx = ship_x - ex, dy = ship_y - ey, dz = ship_z - e->z;
            if (dx*dx + dy*dy + dz*dz < 9.0f) {
                spawn_explosion(ship_x, ship_y, ship_z, HEXPIX(00FF55), 25);
                spawn_explosion(ex, ey, e->z, HEXPIX(FFAA00), 15);
                e->active = false;
                ship_lives--;
                if (ship_lives <= 0) {
                    ship_alive = false;
                    printf("game over! score: %d  waves: %d\n", score, wave);
                } else {
                    ship_x = CX; ship_y = WORLD_Y * 0.75f; ship_z = CZ;
                    ship_vx = ship_vy = ship_vz = 0;
                    ship_invuln = 2.0f;
                }
            }
        }

        // enemy reached player Y level (formation descent too far) => game over progression
        if (!e->diving && (e->y + formation_y) >= WORLD_Y - 6 && ship_alive) {
            ship_lives = 0;
            ship_alive = false;
            printf("the swarm reached you! score: %d\n", score);
        }
    }

    // clear wave?
    if (enemies_alive_count() == 0) {
        wave++;
        printf("wave %d!\n", wave);
        enemies_init_wave(wave);
    }
}

static void enemies_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        enemy_t* e = &enemies[i];
        if (!e->active) continue;
        enemy_t tmp = *e;
        if (!e->diving) {
            tmp.x = e->x + formation_x;
            tmp.y = e->y + formation_y;
        }
        enemy_draw_one(vol, &tmp);
    }
}

// ship-ebullet collision (checked separately so both sides see updates)
static void check_ebullet_hit(void) {
    if (!ship_alive || ship_invuln > 0) return;
    for (int i = 0; i < MAX_EBULLETS; ++i) {
        ebullet_t* b = &ebullets[i];
        if (!b->active) continue;
        float dx = b->x - ship_x, dy = b->y - ship_y, dz = b->z - ship_z;
        if (dx*dx + dy*dy + dz*dz < 4.0f) {
            b->active = false;
            spawn_explosion(ship_x, ship_y, ship_z, HEXPIX(FF3333), 15);
            ship_lives--;
            if (ship_lives <= 0) {
                ship_alive = false;
                printf("game over! score: %d  waves: %d\n", score, wave);
            } else {
                ship_x = CX; ship_y = WORLD_Y * 0.75f; ship_z = CZ;
                ship_vx = ship_vy = ship_vz = 0;
                ship_invuln = 2.0f;
            }
            return;
        }
    }
}

// weapon pickups: a small sphere carrying a pixel-icon that drifts toward the player
#define MAX_PICKUPS 8

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int   weapon;
    bool  active;
    float bob_phase;
} pickup_t;

static pickup_t pickups[MAX_PICKUPS];

static void pickups_init(void) {
    memset(pickups, 0, sizeof(pickups));
}

static void spawn_pickup(float x, float y, float z) {
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (!pickups[i].active) {
            pickups[i] = (pickup_t){
                .x = x, .y = y, .z = z,
                .vx = rand_f(-3.0f, 3.0f),
                .vy = 15.0f,  // drift toward player (+Y)
                .vz = rand_f(-2.0f, 2.0f),
                .weapon = 1 + rand() % (WEAPON_COUNT - 1),  // skip SINGLE
                .active = true,
                .bob_phase = rand_f(0, 6.28f),
            };
            return;
        }
    }
}

static void pickups_update(float dt) {
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        pickup_t* p = &pickups[i];
        if (!p->active) continue;

        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt + sinf(p->bob_phase) * 0.3f;
        p->bob_phase += dt * 3.0f;

        if (p->y > WORLD_Y + 3 || p->x < 0 || p->x >= WORLD_X ||
            p->z < 0 || p->z >= WORLD_Z) {
            p->active = false;
            continue;
        }

        // collect
        if (ship_alive) {
            float dx = p->x - ship_x, dy = p->y - ship_y, dz = p->z - ship_z;
            if (dx*dx + dy*dy + dz*dz < 16.0f) {
                ship_weapon = p->weapon;
                weapon_timer = 12.0f;  // 12s of powered weapon
                spawn_explosion(p->x, p->y, p->z, HEXPIX(FFFFFF), 12);
                p->active = false;
                printf("pickup! weapon=%d\n", p->weapon);
            }
        }
    }
}

static pixel_t weapon_shell_colour(int w) {
    switch (w) {
    case WEAPON_TRIPLE: return HEXPIX(FF8800);
    case WEAPON_LASER:  return HEXPIX(FF55FF);
    case WEAPON_RAPID:  return HEXPIX(FFFF00);
    default:            return HEXPIX(FFFFFF);
    }
}

static void pickups_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        pickup_t* p = &pickups[i];
        if (!p->active) continue;

        int px = (int)p->x, py = (int)p->y, pz = (int)p->z;
        pixel_t shell = weapon_shell_colour(p->weapon);
        pixel_t icon  = HEXPIX(FFFFFF);

        // hollow-ish sphere, radius 2
        for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -2; dz <= 2; ++dz) {
            int d2 = dx*dx + dy*dy + dz*dz;
            if (d2 <= 4 && d2 >= 2) {
                // blink shell for readability
                if (((int)(p->bob_phase * 1.5f) & 1) || d2 == 4) {
                    set_voxel(vol, px + dx, py + dy, pz + dz, shell);
                }
            }
        }

        // weapon icon painted on the player-facing side (+Y face: dy = +2)
        int iy = py + 2;
        switch (p->weapon) {
        case WEAPON_TRIPLE:
            set_voxel(vol, px - 1, iy, pz,     icon);
            set_voxel(vol, px,     iy, pz,     icon);
            set_voxel(vol, px + 1, iy, pz,     icon);
            break;
        case WEAPON_LASER:
            set_voxel(vol, px, iy, pz - 1, icon);
            set_voxel(vol, px, iy, pz,     icon);
            set_voxel(vol, px, iy, pz + 1, icon);
            break;
        case WEAPON_RAPID:
            set_voxel(vol, px - 1, iy, pz + 1, icon);
            set_voxel(vol, px + 1, iy, pz + 1, icon);
            set_voxel(vol, px - 1, iy, pz - 1, icon);
            set_voxel(vol, px + 1, iy, pz - 1, icon);
            set_voxel(vol, px,     iy, pz,     icon);
            break;
        default:
            set_voxel(vol, px, iy, pz, icon);
            break;
        }
    }
}

// particles (explosions / hit sparks)
#define MAX_PARTICLES 768

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

static void spawn_explosion(float px, float py, float pz, pixel_t colour, int count) {
    for (int n = 0; n < count; ++n) {
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!particles[i].active) {
                particle_t* p = &particles[i];
                p->active = true;
                p->x = px; p->y = py; p->z = pz;
                p->vx = rand_f(-40.0f, 40.0f);
                p->vy = rand_f(-40.0f, 40.0f);
                p->vz = rand_f(-40.0f, 40.0f);
                p->life = rand_f(0.3f, 1.0f);
                p->colour = colour;
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
        p->y += p->vy * dt;
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
        if (p->life < 0.3f) {
            int r = R_PIX(c) >> 1, g = G_PIX(c) >> 1, b = B_PIX(c) >> 1;
            c = RGBPIX(r, g, b);
        }
        set_voxel(vol, (int)p->x, (int)p->y, (int)p->z, c);
    }
}

// starfield
#define NUM_STARS 140

typedef struct { float x, y, z; } star_t;
static star_t stars[NUM_STARS];

static void stars_init(void) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].x = rand_f(0, WORLD_X);
        stars[i].y = rand_f(0, WORLD_Y);
        stars[i].z = rand_f(0, WORLD_Z);
    }
}

static void stars_update(float dt) {
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].y += 12.0f * dt;
        if (stars[i].y >= WORLD_Y) {
            stars[i].y -= WORLD_Y;
            stars[i].x = rand_f(0, WORLD_X);
            stars[i].z = rand_f(0, WORLD_Z);
        }
    }
}

static void stars_draw(pixel_t* vol) {
    for (int i = 0; i < NUM_STARS; ++i) {
        int sx = (int)stars[i].x, sy = (int)stars[i].y, sz = (int)stars[i].z;
        if ((uint32_t)sx < WORLD_X && (uint32_t)sy < WORLD_Y && (uint32_t)sz < WORLD_Z) {
            vol[VOXEL_INDEX(sx, sy, sz)] = HEXPIX(555555);
        }
    }
}

// HUD
static void hud_draw(pixel_t* vol) {
    // lives in a corner
    for (int i = 0; i < ship_lives && i < 5; ++i) {
        set_voxel(vol, 2 + i * 3, WORLD_Y - 3, WORLD_Z - 2, HEXPIX(FF0000));
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, WORLD_Z - 2, HEXPIX(FF0000));
        set_voxel(vol, 2 + i * 3, WORLD_Y - 3, WORLD_Z - 3, HEXPIX(FF0000));
        set_voxel(vol, 3 + i * 3, WORLD_Y - 3, WORLD_Z - 3, HEXPIX(FF0000));
    }

    // weapon indicator (opposite corner) — small coloured bar whose length = timer
    if (ship_weapon != WEAPON_SINGLE && weapon_timer > 0) {
        pixel_t c = weapon_shell_colour(ship_weapon);
        int len = (int)(weapon_timer * 0.8f);
        if (len > 10) len = 10;
        for (int i = 0; i < len; ++i) {
            set_voxel(vol, WORLD_X - 3 - i, WORLD_Y - 3, WORLD_Z - 2, c);
        }
    }
}

// game state
static float gameover_timer = 0;

static void game_reset(void) {
    ship_init();
    ship_lives = 3;
    bullets_init();
    ebullets_init();
    particles_init();
    pickups_init();
    stars_init();
    keys_clear();
    wave = 1;
    score = 0;
    enemies_init_wave(wave);
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

    printf("=== SPACE SHOOTER ===\n");
    printf("  WASD  move\n");
    printf("  Z/X   descend/ascend (Z-axis)\n");
    printf("  SPACE fire\n");
    printf("  R     restart\n");
    printf("  ESC   quit\n");
    printf("\n");
    printf("  pick up the glowing spheres for triple-shot, laser, rapid-fire!\n\n");

    input_set_nonblocking();

    for (int ch = 0; ch != 27; ch = getchar()) {
        timer_tick();
        float dt = (float)timer_delta_time * 0.001f;
        dt = clamp(dt, 0.001f, 0.1f);

        switch (ch) {
            case 'w': key_w = true;  break;
            case 'a': key_a = true;  break;
            case 's': key_s = true;  break;
            case 'd': key_d = true;  break;
            case 'z': key_z = true;  break;
            case 'x': key_x = true;  break;
            case ' ': key_space = true; break;
            case 'r':
            case 'R':
                game_reset();
                printf("restarted!\n");
                break;
        }

        input_update();

        ship_update(dt);
        bullets_update(dt);
        ebullets_update(dt);
        enemies_update(dt);
        pickups_update(dt);
        particles_update(dt);
        stars_update(dt);
        check_ebullet_hit();

        if (!ship_alive) {
            gameover_timer += dt;
            if (gameover_timer > 5.0f || input_get_button(0, BUTTON_A, BUTTON_PRESSED)) {
                game_reset();
            }
        }

        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        stars_draw(vol);
        enemies_draw(vol);
        pickups_draw(vol);
        bullets_draw(vol);
        ebullets_draw(vol);
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
