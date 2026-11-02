// Vampire Survivors clone: circle hero, triangle horde.
// Build (Linux):  g++ -O2 -std=c++17 main.cpp -o survivors $(pkg-config --cflags --libs raylib)
//             or: g++ -O2 -std=c++17 main.cpp -o survivors -lraylib -lm -lpthread -ldl -lrt -lX11
// Controls: WASD / arrows to move. Attacks are automatic. 1/2/3 or click to pick upgrades. R restarts.

#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const int MAX_ENEMIES = 500;

// ---------- helpers ----------
static float Rnd(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

static void DrawTri(Vector2 c, float r, float angle, Color col, bool outline = true) {
    Vector2 a = {c.x + cosf(angle) * r, c.y + sinf(angle) * r};
    Vector2 b = {c.x + cosf(angle + 2.4f) * r, c.y + sinf(angle + 2.4f) * r};
    Vector2 d = {c.x + cosf(angle - 2.4f) * r, c.y + sinf(angle - 2.4f) * r};
    // raylib wants counter-clockwise winding (y-down screen space => negative cross product)
    float cross = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
    if (cross > 0) std::swap(b, d);
    DrawTriangle(a, b, d, col);
    if (outline) DrawTriangleLines(a, b, d, Fade(BLACK, 0.5f));
}

// ---------- data ----------
struct Player {
    Vector2 pos{0, 0};
    Vector2 facing{1, 0};
    float radius = 16, speed = 230;
    float hp = 100, maxHp = 100, regen = 0, invuln = 0;
    float pickupRadius = 90;
    int level = 1, xp = 0, xpNext = 5;
    // magic bolt
    float fireRate = 1.6f, fireTimer = 0, damage = 3;
    int projectiles = 1, pierce = 0;
    // orbiting orbs
    int orbs = 0;
    float orbAngle = 0, orbDamage = 4;
};

struct Enemy {
    int id;
    Vector2 pos, knock{0, 0};
    float hp, maxHp, speed, size, flash = 0, orbCd = 0, contactDmg;
    int gemValue;
    Color color;
    bool dead = false;
};

struct Bullet {
    Vector2 pos, vel;
    float life, dmg;
    int pierce;
    std::vector<int> hit;
};

struct Gem { Vector2 pos; int value; };
struct Particle { Vector2 pos, vel; float life, maxLife; Color color; };

enum State { PLAYING, LEVELUP, DEAD };

static const char *UP_NAME[9] = {"Rapid Fire", "Heavy Bolts", "Multishot", "Piercing", "Orbiting Orb",
                                 "Swift Feet", "Vitality", "Magnet", "Regeneration"};
static const char *UP_DESC[9] = {"+25% fire rate", "+30% bolt damage", "+1 projectile", "Bolts pierce +1 enemy",
                                 "+1 orb circles you", "+12% move speed", "+25 max HP and heal", "+40% pickup range",
                                 "+0.5 HP per second"};

struct Game {
    Player p;
    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    std::vector<Gem> gems;
    std::vector<Particle> particles;
    State state = PLAYING;
    float time = 0, spawnTimer = 0, shake = 0;
    int kills = 0, nextId = 1;
    int choices[3] = {0, 1, 2};

    void Reset() {
        p = Player();
        enemies.clear(); bullets.clear(); gems.clear(); particles.clear();
        state = PLAYING; time = 0; spawnTimer = 0; shake = 0; kills = 0; nextId = 1;
    }

    // ----- upgrades -----
    void RollChoices() {
        int pool[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        for (int i = 8; i > 0; i--) std::swap(pool[i], pool[GetRandomValue(0, i)]);
        for (int i = 0; i < 3; i++) choices[i] = pool[i];
    }

    void Apply(int id) {
        switch (id) {
            case 0: p.fireRate *= 1.25f; break;
            case 1: p.damage *= 1.3f; p.orbDamage *= 1.3f; break;
            case 2: p.projectiles++; break;
            case 3: p.pierce++; break;
            case 4: p.orbs++; break;
            case 5: p.speed *= 1.12f; break;
            case 6: p.maxHp += 25; p.hp = std::min(p.maxHp, p.hp + 25); break;
            case 7: p.pickupRadius *= 1.4f; break;
            case 8: p.regen += 0.5f; break;
        }
    }

    void CheckLevelUp() {
        if (state == PLAYING && p.xp >= p.xpNext) {
            p.xp -= p.xpNext;
            p.level++;
            p.xpNext = 5 + p.level * 4;
            RollChoices();
            state = LEVELUP;
        }
    }

    // ----- spawning -----
    void SpawnEnemy() {
        if ((int)enemies.size() >= MAX_ENEMIES) return;
        float ang = Rnd(0, 2 * PI);
        float dist = sqrtf((float)(SCREEN_W * SCREEN_W + SCREEN_H * SCREEN_H)) * 0.5f + 60;
        Enemy e;
        e.id = nextId++;
        e.pos = {p.pos.x + cosf(ang) * dist, p.pos.y + sinf(ang) * dist};
        float hp = 3 + time * 0.05f;
        float spd = std::min(110.0f, 70 + time * 0.25f) * Rnd(0.85f, 1.15f);
        float r = Rnd(0, 1);
        if (time > 60 && r < 0.15f) {          // tank
            e.size = 24; e.hp = hp * 4; e.speed = spd * 0.6f; e.contactDmg = 20; e.gemValue = 3; e.color = {150, 70, 200, 255};
        } else if (time > 25 && r < 0.40f) {   // fast
            e.size = 10; e.hp = hp * 0.7f; e.speed = spd * 1.7f; e.contactDmg = 8; e.gemValue = 1; e.color = {240, 200, 60, 255};
        } else {                               // grunt
            e.size = 14; e.hp = hp; e.speed = spd; e.contactDmg = 10; e.gemValue = 1; e.color = {220, 60, 60, 255};
        }
        e.maxHp = e.hp;
        enemies.push_back(e);
    }

    void Burst(Vector2 pos, Color c, int n) {
        for (int i = 0; i < n; i++) {
            float a = Rnd(0, 2 * PI), s = Rnd(40, 180);
            float life = Rnd(0.25f, 0.6f);
            particles.push_back({pos, {cosf(a) * s, sinf(a) * s}, life, life, c});
        }
    }

    void Hurt(Enemy &e, float dmg, Vector2 kickDir, float kick) {
        if (e.dead) return;
        e.hp -= dmg;
        e.flash = 0.1f;
        e.knock = Vector2Add(e.knock, Vector2Scale(kickDir, kick));
        if (e.hp <= 0) {
            e.dead = true;
            kills++;
            gems.push_back({e.pos, e.gemValue});
            Burst(e.pos, e.color, 8);
        }
    }

    // ----- update -----
    void Update(float dt) {
        if (state == DEAD) { if (IsKeyPressed(KEY_R)) Reset(); return; }
        if (state == LEVELUP) {
            int pick = -1;
            if (IsKeyPressed(KEY_ONE)) pick = 0;
            if (IsKeyPressed(KEY_TWO)) pick = 1;
            if (IsKeyPressed(KEY_THREE)) pick = 2;
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 m = GetMousePosition();
                for (int i = 0; i < 3; i++) if (CheckCollisionPointRec(m, CardRect(i))) pick = i;
            }
            if (pick >= 0) { Apply(choices[pick]); state = PLAYING; CheckLevelUp(); }
            return;
        }

        time += dt;
        shake = std::max(0.0f, shake - dt);

        // player movement
        Vector2 mv = {0, 0};
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) mv.y -= 1;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) mv.y += 1;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) mv.x -= 1;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mv.x += 1;
        if (mv.x != 0 || mv.y != 0) {
            mv = Vector2Normalize(mv);
            p.facing = mv;
            p.pos = Vector2Add(p.pos, Vector2Scale(mv, p.speed * dt));
        }
        p.invuln = std::max(0.0f, p.invuln - dt);
        p.hp = std::min(p.maxHp, p.hp + p.regen * dt);

        // spawning
        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            spawnTimer = std::max(0.08f, 0.9f - time * 0.006f);
            int n = 1 + (int)(time / 45.0f);
            for (int i = 0; i < n; i++) SpawnEnemy();
        }

        // auto-fire at nearest enemy
        p.fireTimer -= dt;
        if (p.fireTimer <= 0) {
            const Enemy *best = nullptr;
            float bestD = 650 * 650;
            for (const Enemy &e : enemies) {
                float d = Vector2DistanceSqr(e.pos, p.pos);
                if (d < bestD) { bestD = d; best = &e; }
            }
            if (best) {
                p.fireTimer = 1.0f / p.fireRate;
                Vector2 dir = Vector2Normalize(Vector2Subtract(best->pos, p.pos));
                float base = atan2f(dir.y, dir.x);
                for (int i = 0; i < p.projectiles; i++) {
                    float a = base + (i - (p.projectiles - 1) * 0.5f) * 0.16f;
                    Bullet b;
                    b.pos = p.pos;
                    b.vel = {cosf(a) * 650, sinf(a) * 650};
                    b.life = 1.4f; b.dmg = p.damage; b.pierce = p.pierce;
                    bullets.push_back(b);
                }
            } else {
                p.fireTimer = 0.1f;
            }
        }

        // bullets
        for (Bullet &b : bullets) {
            b.pos = Vector2Add(b.pos, Vector2Scale(b.vel, dt));
            b.life -= dt;
            for (Enemy &e : enemies) {
                if (e.dead) continue;
                if (!CheckCollisionCircles(b.pos, 5, e.pos, e.size * 0.8f)) continue;
                if (std::find(b.hit.begin(), b.hit.end(), e.id) != b.hit.end()) continue;
                b.hit.push_back(e.id);
                Hurt(e, b.dmg, Vector2Normalize(b.vel), 120);
                if (b.pierce-- <= 0) { b.life = 0; break; }
            }
        }
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet &b) { return b.life <= 0; }),
                      bullets.end());

        // orbiting orbs
        p.orbAngle += 3.2f * dt;
        for (int i = 0; i < p.orbs; i++) {
            float a = p.orbAngle + i * (2 * PI / p.orbs);
            Vector2 op = {p.pos.x + cosf(a) * 95, p.pos.y + sinf(a) * 95};
            for (Enemy &e : enemies) {
                if (e.dead || e.orbCd > 0) continue;
                if (CheckCollisionCircles(op, 11, e.pos, e.size * 0.8f)) {
                    e.orbCd = 0.4f;
                    Hurt(e, p.orbDamage, Vector2Normalize(Vector2Subtract(e.pos, p.pos)), 220);
                }
            }
        }

        // enemies
        for (Enemy &e : enemies) {
            if (e.dead) continue;
            Vector2 dir = Vector2Normalize(Vector2Subtract(p.pos, e.pos));
            e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Add(Vector2Scale(dir, e.speed), e.knock), dt));
            e.knock = Vector2Scale(e.knock, std::max(0.0f, 1 - 8 * dt));
            e.flash -= dt;
            e.orbCd -= dt;
            if (p.invuln <= 0 && CheckCollisionCircles(p.pos, p.radius, e.pos, e.size * 0.7f)) {
                p.hp -= e.contactDmg;
                p.invuln = 0.6f;
                shake = 0.2f;
                if (p.hp <= 0) { p.hp = 0; state = DEAD; }
            }
        }
        // soft separation so the horde doesn't collapse into one triangle
        for (size_t i = 0; i < enemies.size(); i++) {
            for (size_t j = i + 1; j < enemies.size(); j++) {
                Enemy &a = enemies[i], &b = enemies[j];
                float dx = b.pos.x - a.pos.x, dy = b.pos.y - a.pos.y;
                float minD = (a.size + b.size) * 0.7f;
                float d2 = dx * dx + dy * dy;
                if (d2 > 0.0001f && d2 < minD * minD) {
                    float d = sqrtf(d2), push = (minD - d) * 0.5f;
                    dx /= d; dy /= d;
                    a.pos.x -= dx * push; a.pos.y -= dy * push;
                    b.pos.x += dx * push; b.pos.y += dy * push;
                }
            }
        }
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy &e) { return e.dead; }),
                      enemies.end());

        // gems
        for (Gem &g : gems) {
            float d = Vector2Distance(g.pos, p.pos);
            if (d < p.pickupRadius) {
                Vector2 dir = Vector2Normalize(Vector2Subtract(p.pos, g.pos));
                g.pos = Vector2Add(g.pos, Vector2Scale(dir, 420 * dt));
            }
            if (d < p.radius + 6) { p.xp += g.value; g.value = 0; }
        }
        gems.erase(std::remove_if(gems.begin(), gems.end(), [](const Gem &g) { return g.value == 0; }), gems.end());
        CheckLevelUp();

        // particles
        for (Particle &pt : particles) {
            pt.pos = Vector2Add(pt.pos, Vector2Scale(pt.vel, dt));
            pt.vel = Vector2Scale(pt.vel, 1 - 4 * dt);
            pt.life -= dt;
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle &q) { return q.life <= 0; }),
                        particles.end());
    }

    // ----- drawing -----
    static Rectangle CardRect(int i) { return {SCREEN_W / 2.0f - 510 + i * 350.0f, SCREEN_H / 2.0f - 110, 320, 220}; }

    void DrawWorld(const Camera2D &cam) {
        // grid
        const int S = 64;
        int x0 = (int)floorf((cam.target.x - SCREEN_W / 2.0f) / S) * S - S;
        int y0 = (int)floorf((cam.target.y - SCREEN_H / 2.0f) / S) * S - S;
        for (int x = x0; x < x0 + SCREEN_W + 3 * S; x += S) DrawLine(x, y0, x, y0 + SCREEN_H + 3 * S, {38, 40, 52, 255});
        for (int y = y0; y < y0 + SCREEN_H + 3 * S; y += S) DrawLine(x0, y, x0 + SCREEN_W + 3 * S, y, {38, 40, 52, 255});

        for (const Gem &g : gems) {
            DrawPoly(g.pos, 4, 6, 45, g.value > 1 ? GOLD : SKYBLUE);
        }

        DrawCircleLinesV(p.pos, p.pickupRadius, Fade(WHITE, 0.06f));

        for (const Enemy &e : enemies) {
            Vector2 d = Vector2Subtract(p.pos, e.pos);
            float ang = atan2f(d.y, d.x);
            DrawTri(e.pos, e.size, ang, e.flash > 0 ? WHITE : e.color);
        }

        for (int i = 0; i < p.orbs; i++) {
            float a = p.orbAngle + i * (2 * PI / p.orbs);
            Vector2 op = {p.pos.x + cosf(a) * 95, p.pos.y + sinf(a) * 95};
            DrawCircleV(op, 11, {255, 160, 40, 255});
            DrawCircleLinesV(op, 11, WHITE);
        }

        for (const Bullet &b : bullets) {
            DrawCircleV(b.pos, 5, SKYBLUE);
            DrawCircleV(b.pos, 8, Fade(SKYBLUE, 0.25f));
        }

        for (const Particle &pt : particles) {
            DrawCircleV(pt.pos, 3, Fade(pt.color, pt.life / pt.maxLife));
        }

        // player
        bool blink = p.invuln > 0 && ((int)(p.invuln * 20) % 2 == 0);
        DrawCircleV(p.pos, p.radius, blink ? Fade(WHITE, 0.4f) : Color{80, 200, 120, 255});
        DrawCircleLinesV(p.pos, p.radius, WHITE);
        DrawCircleV(Vector2Add(p.pos, Vector2Scale(p.facing, 8)), 4, BLACK);
    }

    void DrawHUD() {
        // xp bar
        DrawRectangle(0, 0, SCREEN_W, 14, {20, 20, 30, 255});
        DrawRectangle(0, 0, (int)(SCREEN_W * (float)p.xp / p.xpNext), 14, {70, 140, 255, 255});
        DrawText(TextFormat("LV %d", p.level), SCREEN_W - 70, 18, 20, WHITE);

        // hp bar
        DrawRectangle(20, SCREEN_H - 36, 260, 18, {60, 20, 20, 255});
        DrawRectangle(20, SCREEN_H - 36, (int)(260 * p.hp / p.maxHp), 18, {220, 50, 50, 255});
        DrawRectangleLines(20, SCREEN_H - 36, 260, 18, WHITE);
        DrawText(TextFormat("%d / %d", (int)ceilf(p.hp), (int)p.maxHp), 28, SCREEN_H - 34, 14, WHITE);

        int m = (int)time / 60, s = (int)time % 60;
        const char *t = TextFormat("%02d:%02d", m, s);
        DrawText(t, SCREEN_W / 2 - MeasureText(t, 32) / 2, 22, 32, WHITE);
        DrawText(TextFormat("Kills: %d", kills), 14, 22, 20, WHITE);
        DrawText(TextFormat("FPS: %d  Enemies: %d", GetFPS(), (int)enemies.size()), 14, 46, 14, GRAY);
    }

    void DrawOverlay() {
        if (state == LEVELUP) {
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.7f));
            const char *title = "LEVEL UP!";
            DrawText(title, SCREEN_W / 2 - MeasureText(title, 48) / 2, SCREEN_H / 2 - 190, 48, GOLD);
            Vector2 m = GetMousePosition();
            for (int i = 0; i < 3; i++) {
                Rectangle r = CardRect(i);
                bool hov = CheckCollisionPointRec(m, r);
                DrawRectangleRec(r, hov ? Color{60, 64, 90, 255} : Color{34, 36, 54, 255});
                DrawRectangleLinesEx(r, 3, hov ? GOLD : GRAY);
                DrawText(TextFormat("[%d]", i + 1), (int)r.x + 14, (int)r.y + 12, 20, GRAY);
                const char *n = UP_NAME[choices[i]];
                DrawText(n, (int)(r.x + r.width / 2 - MeasureText(n, 28) / 2), (int)r.y + 80, 28, WHITE);
                const char *d = UP_DESC[choices[i]];
                DrawText(d, (int)(r.x + r.width / 2 - MeasureText(d, 18) / 2), (int)r.y + 130, 18, LIGHTGRAY);
            }
        } else if (state == DEAD) {
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.75f));
            const char *a = "YOU DIED";
            DrawText(a, SCREEN_W / 2 - MeasureText(a, 64) / 2, SCREEN_H / 2 - 100, 64, RED);
            const char *b = TextFormat("Survived %02d:%02d   Kills: %d   Level: %d", (int)time / 60, (int)time % 60, kills, p.level);
            DrawText(b, SCREEN_W / 2 - MeasureText(b, 24) / 2, SCREEN_H / 2, 24, WHITE);
            const char *c = "Press R to restart";
            DrawText(c, SCREEN_W / 2 - MeasureText(c, 22) / 2, SCREEN_H / 2 + 50, 22, GRAY);
        }
    }
};

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Triangle Survivors");
    SetTargetFPS(60);

    Game game;
    game.Reset();

    Camera2D cam{};
    cam.zoom = 1.0f;

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.05f);
        game.Update(dt);

        cam.target = game.p.pos;
        float sh = game.shake * 30.0f;
        cam.offset = {SCREEN_W / 2.0f + Rnd(-sh, sh), SCREEN_H / 2.0f + Rnd(-sh, sh)};

        BeginDrawing();
        ClearBackground({22, 24, 32, 255});
        BeginMode2D(cam);
        game.DrawWorld(cam);
        EndMode2D();
        game.DrawHUD();
        game.DrawOverlay();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
