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

// keyboard state
static bool key_w, key_a, key_s, key_d, key_z, key_x;

static void keys_clear(void) {
    key_w = key_a = key_s = key_d = key_z = key_x = false;
}

static float rand_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static inline void set_voxel(pixel_t* vol, int x, int y, int z, pixel_t c) {
    if ((uint32_t)x < WORLD_X && (uint32_t)y < WORLD_Y && (uint32_t)z < WORLD_Z) {
        vol[VOXEL_INDEX(x, y, z)] = c;
    }
}

// Cool palette is reserved for the player. Enemies pick only from warm hues
// so the player can always spot themselves at a glance.
#define ENEMY_PALETTE_SIZE 6
static const pixel_t enemy_palette[ENEMY_PALETTE_SIZE] = {
    HEXPIX(FF2222),  // red
    HEXPIX(FF8800),  // orange
    HEXPIX(FFDD00),  // yellow
    HEXPIX(FF22AA),  // hot pink
    HEXPIX(AA00FF),  // purple
    HEXPIX(FF5500),  // amber
};

// shell colour stays the body colour at full brightness; dim it for the inside fill
static pixel_t dim_pix(pixel_t c, int shift) {
    int r = R_PIX(c) >> shift;
    int g = G_PIX(c) >> shift;
    int b = B_PIX(c) >> shift;
    return RGBPIX(r, g, b);
}

// draw a sphere: bright shell, dimmer fill, optional bright core marker
static void draw_ball(pixel_t* vol, float fx, float fy, float fz, float fr,
                      pixel_t shell, pixel_t core, bool has_core) {
    int cx = (int)fx, cy = (int)fy, cz = (int)fz;
    int r  = (int)(fr + 0.5f);
    if (r < 1) r = 1;

    pixel_t fill = dim_pix(shell, 2);     // ~25% brightness inside
    int r2_outer = r * r;
    int r2_inner = (r - 1) * (r - 1);

    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        int d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > r2_outer) continue;
        pixel_t c = (d2 > r2_inner) ? shell : fill;
        set_voxel(vol, cx + dx, cy + dy, cz + dz, c);
    }

    if (has_core) {
        set_voxel(vol, cx, cy, cz, core);
    }
}

// food: tiny dim pellets scattered through the volume
#define MAX_FOOD 220

typedef struct {
    float x, y, z;
    pixel_t colour;
    bool active;
} food_t;

static food_t food[MAX_FOOD];

static void food_spawn_one(int i) {
    food[i].x = rand_f(2.0f, WORLD_X - 3.0f);
    food[i].y = rand_f(2.0f, WORLD_Y - 3.0f);
    food[i].z = rand_f(2.0f, WORLD_Z - 3.0f);
    // food is intentionally muted — never confused with a ball
    static const pixel_t food_palette[] = {
        HEXPIX(444444), HEXPIX(335533), HEXPIX(553333),
        HEXPIX(333355), HEXPIX(555533), HEXPIX(553355),
    };
    food[i].colour = food_palette[rand() % (int)count_of(food_palette)];
    food[i].active = true;
}

static void food_init(void) {
    for (int i = 0; i < MAX_FOOD; ++i) food_spawn_one(i);
}

static void food_draw(pixel_t* vol) {
    for (int i = 0; i < MAX_FOOD; ++i) {
        if (!food[i].active) continue;
        set_voxel(vol, (int)food[i].x, (int)food[i].y, (int)food[i].z, food[i].colour);
    }
}

// balls: one is the player, the rest are AI enemies
#define MAX_BALLS  16
#define PLAYER_IDX 0

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float mass;            // radius derived from mass
    pixel_t shell;
    bool active;
    bool is_player;
    // AI state
    float think_timer;
    float dx, dy, dz;      // current AI heading
} ball_t;

static ball_t balls[MAX_BALLS];

static const float ball_friction   = 0.92f;
static const float player_accel    = 220.0f;
static const float enemy_accel     = 140.0f;

static float mass_to_radius(float m) {
    // r grows with cube root of mass — same intuition as 2D agar's area rule
    return powf(m, 1.0f / 3.0f) + 0.5f;
}

static float speed_for_mass(float m) {
    // bigger = slower, but never glacial
    float r = mass_to_radius(m);
    float s = 60.0f / (r * 0.45f + 1.0f);
    if (s < 14.0f) s = 14.0f;
    return s;
}

static void ball_spawn_enemy(int i) {
    ball_t* b = &balls[i];
    b->active = true;
    b->is_player = false;
    b->x = rand_f(6.0f, WORLD_X - 7.0f);
    b->y = rand_f(6.0f, WORLD_Y - 7.0f);
    b->z = rand_f(4.0f, WORLD_Z - 5.0f);
    b->vx = b->vy = b->vz = 0;
    b->mass = rand_f(2.0f, 12.0f);
    b->shell = enemy_palette[rand() % ENEMY_PALETTE_SIZE];
    b->think_timer = 0;
    b->dx = b->dy = b->dz = 0;
}

static void balls_init(void) {
    memset(balls, 0, sizeof(balls));

    // player: cool, distinctive cyan-green — never used by enemies
    ball_t* p = &balls[PLAYER_IDX];
    p->active = true;
    p->is_player = true;
    p->x = CX; p->y = CY; p->z = CZ;
    p->vx = p->vy = p->vz = 0;
    p->mass = 4.0f;
    p->shell = HEXPIX(00FFAA);

    for (int i = 1; i < MAX_BALLS; ++i) ball_spawn_enemy(i);
}

static void clamp_ball(ball_t* b) {
    float r = mass_to_radius(b->mass);
    if (b->x < r)             { b->x = r;             b->vx = 0; }
    if (b->x > WORLD_X - 1 - r){ b->x = WORLD_X - 1 - r; b->vx = 0; }
    if (b->y < r)             { b->y = r;             b->vy = 0; }
    if (b->y > WORLD_Y - 1 - r){ b->y = WORLD_Y - 1 - r; b->vy = 0; }
    if (b->z < r)             { b->z = r;             b->vz = 0; }
    if (b->z > WORLD_Z - 1 - r){ b->z = WORLD_Z - 1 - r; b->vz = 0; }
}

static void player_update(float dt) {
    ball_t* p = &balls[PLAYER_IDX];
    if (!p->active) return;

    float ax = 0, ay = 0, az = 0;
    if (key_d) ax += 1; if (key_a) ax -= 1;
    if (key_s) ay += 1; if (key_w) ay -= 1;
    if (key_x) az += 1; if (key_z) az -= 1;

    ax += input_get_axis(0, AXIS_LS_X);
    ay += input_get_axis(0, AXIS_LS_Y);
    az += input_get_axis(0, AXIS_RT) - input_get_axis(0, AXIS_LT);

    p->vx = (p->vx + ax * player_accel * dt) * ball_friction;
    p->vy = (p->vy + ay * player_accel * dt) * ball_friction;
    p->vz = (p->vz + az * player_accel * dt) * ball_friction;

    float max_s = speed_for_mass(p->mass);
    float s2 = p->vx*p->vx + p->vy*p->vy + p->vz*p->vz;
    if (s2 > max_s * max_s) {
        float k = max_s / sqrtf(s2);
        p->vx *= k; p->vy *= k; p->vz *= k;
    }

    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->z += p->vz * dt;
    clamp_ball(p);
}

// AI: head toward the nearest smaller ball, flee from the nearest bigger one,
// otherwise wander toward food or a random direction.
static void ai_pick_target(int i) {
    ball_t* me = &balls[i];

    float best_prey_d2 = 1e9f, best_pred_d2 = 1e9f;
    int   prey = -1, pred = -1;

    for (int j = 0; j < MAX_BALLS; ++j) {
        if (j == i || !balls[j].active) continue;
        float dx = balls[j].x - me->x;
        float dy = balls[j].y - me->y;
        float dz = balls[j].z - me->z;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > 30.0f * 30.0f) continue;        // limited senses

        if (balls[j].mass * 1.15f < me->mass) {
            if (d2 < best_prey_d2) { best_prey_d2 = d2; prey = j; }
        } else if (me->mass * 1.15f < balls[j].mass) {
            if (d2 < best_pred_d2) { best_pred_d2 = d2; pred = j; }
        }
    }

    float tx = me->x, ty = me->y, tz = me->z;
    bool have_target = false;

    if (pred >= 0 && best_pred_d2 < 18.0f * 18.0f) {
        // run away
        tx = me->x - (balls[pred].x - me->x);
        ty = me->y - (balls[pred].y - me->y);
        tz = me->z - (balls[pred].z - me->z);
        have_target = true;
    } else if (prey >= 0) {
        tx = balls[prey].x; ty = balls[prey].y; tz = balls[prey].z;
        have_target = true;
    } else {
        // hunt nearest food
        float best = 1e9f;
        int   bf   = -1;
        for (int k = 0; k < MAX_FOOD; ++k) {
            if (!food[k].active) continue;
            float dx = food[k].x - me->x;
            float dy = food[k].y - me->y;
            float dz = food[k].z - me->z;
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 < best) { best = d2; bf = k; }
        }
        if (bf >= 0) {
            tx = food[bf].x; ty = food[bf].y; tz = food[bf].z;
            have_target = true;
        }
    }

    if (!have_target) {
        tx = me->x + rand_f(-20.0f, 20.0f);
        ty = me->y + rand_f(-20.0f, 20.0f);
        tz = me->z + rand_f(-15.0f, 15.0f);
    }

    float dx = tx - me->x, dy = ty - me->y, dz = tz - me->z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len > 0.001f) { me->dx = dx/len; me->dy = dy/len; me->dz = dz/len; }
}

static void enemies_update(float dt) {
    for (int i = 0; i < MAX_BALLS; ++i) {
        ball_t* b = &balls[i];
        if (!b->active || b->is_player) continue;

        b->think_timer -= dt;
        if (b->think_timer <= 0) {
            ai_pick_target(i);
            b->think_timer = rand_f(0.3f, 0.8f);
        }

        b->vx = (b->vx + b->dx * enemy_accel * dt) * ball_friction;
        b->vy = (b->vy + b->dy * enemy_accel * dt) * ball_friction;
        b->vz = (b->vz + b->dz * enemy_accel * dt) * ball_friction;

        float max_s = speed_for_mass(b->mass);
        float s2 = b->vx*b->vx + b->vy*b->vy + b->vz*b->vz;
        if (s2 > max_s * max_s) {
            float k = max_s / sqrtf(s2);
            b->vx *= k; b->vy *= k; b->vz *= k;
        }

        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->z += b->vz * dt;
        clamp_ball(b);
    }
}

// eating: balls vs food, balls vs balls
static void resolve_eating(void) {
    // food pickup
    for (int i = 0; i < MAX_BALLS; ++i) {
        ball_t* b = &balls[i];
        if (!b->active) continue;
        float r = mass_to_radius(b->mass);
        float r2 = (r + 0.7f) * (r + 0.7f);

        for (int k = 0; k < MAX_FOOD; ++k) {
            if (!food[k].active) continue;
            float dx = food[k].x - b->x;
            float dy = food[k].y - b->y;
            float dz = food[k].z - b->z;
            if (dx*dx + dy*dy + dz*dz < r2) {
                b->mass += 0.4f;
                food_spawn_one(k);   // recycle pellet elsewhere
            }
        }
    }

    // ball vs ball — bigger eats smaller (needs 1.15x mass)
    for (int i = 0; i < MAX_BALLS; ++i) {
        ball_t* a = &balls[i];
        if (!a->active) continue;
        for (int j = 0; j < MAX_BALLS; ++j) {
            if (i == j || !balls[j].active) continue;
            ball_t* c = &balls[j];
            if (a->mass < c->mass * 1.15f) continue;     // not big enough

            float dx = c->x - a->x, dy = c->y - a->y, dz = c->z - a->z;
            float d2 = dx*dx + dy*dy + dz*dz;
            float ra = mass_to_radius(a->mass);
            // must overlap centre-into-body to count as a swallow
            if (d2 < ra * ra * 0.65f) {
                a->mass += c->mass * 0.85f;
                c->active = false;

                if (c->is_player) {
                    printf("you got eaten! final mass: %.1f\n", a->mass);
                } else if (a->is_player) {
                    printf("ate enemy! mass: %.1f\n", a->mass);
                }
            }
        }
    }

    // respawn dead enemies after a beat so the world stays lively
    for (int i = 1; i < MAX_BALLS; ++i) {
        if (!balls[i].active) {
            ball_spawn_enemy(i);
            // start small so they aren't immediately threatening
            balls[i].mass = rand_f(2.0f, 5.0f);
        }
    }
}

static void balls_draw(pixel_t* vol) {
    // draw enemies first so the player ends up on top in case of overlap
    for (int i = 0; i < MAX_BALLS; ++i) {
        ball_t* b = &balls[i];
        if (!b->active || b->is_player) continue;
        draw_ball(vol, b->x, b->y, b->z, mass_to_radius(b->mass),
                  b->shell, HEXPIX(000000), false);
    }
    ball_t* p = &balls[PLAYER_IDX];
    if (p->active) {
        // bright white core makes the player unmistakable from any angle
        draw_ball(vol, p->x, p->y, p->z, mass_to_radius(p->mass),
                  p->shell, HEXPIX(FFFFFF), true);
    }
}

static void game_reset(void) {
    food_init();
    balls_init();
    keys_clear();
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do { sleep(1); } while (!voxel_buffer_map());
    }

    timer_init();
    game_reset();

    printf("=== AGAR 3D ===\n");
    printf("  WASD  move in XY\n");
    printf("  Z/X   move in Z (down/up)\n");
    printf("  R     restart\n");
    printf("  ESC   quit\n");
    printf("\n");
    printf("  cyan ball with white core = YOU. eat smaller, dodge bigger.\n\n");

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
            case 'z': key_z = true; break;
            case 'x': key_x = true; break;
            case 'r':
            case 'R':
                game_reset();
                printf("restarted!\n");
                break;
        }

        input_update();

        player_update(dt);
        enemies_update(dt);
        resolve_eating();

        // if the player got eaten, respawn them small after a moment
        if (!balls[PLAYER_IDX].active) {
            balls[PLAYER_IDX].active = true;
            balls[PLAYER_IDX].x = CX;
            balls[PLAYER_IDX].y = CY;
            balls[PLAYER_IDX].z = CZ;
            balls[PLAYER_IDX].vx = balls[PLAYER_IDX].vy = balls[PLAYER_IDX].vz = 0;
            balls[PLAYER_IDX].mass = 4.0f;
        }

        pixel_t* vol = voxel_buffer_get(VOXEL_BUFFER_BACK);
        voxel_buffer_clear(vol);

        food_draw(vol);
        balls_draw(vol);

        voxel_buffer_swap();

        keys_clear();
        timer_sleep_until(TIMER_SINCE_TICK, 30);
    }

    voxel_buffer_unmap();
    return 0;
}
