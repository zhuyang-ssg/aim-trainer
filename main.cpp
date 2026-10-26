#include "raylib.h"
#include <vector>
#include <algorithm>
#include <ctime>
#include <cmath>

// ================= 常量 =================
static const int   SCREEN_W   = 1280;
static const int   SCREEN_H   = 720;
static const float ROUND_TIME = 60.0f;
static const int   MAX_BALLS  = 12;

static const Rectangle PAUSE_BTN    = { SCREEN_W - 72.0f, 20.0f, 48.0f, 48.0f };
static const Rectangle CONTINUE_BTN = { SCREEN_W * 0.5f - 110.0f, SCREEN_H * 0.5f + 10.0f, 220.0f, 64.0f };
static const Rectangle RESTART_BTN  = { SCREEN_W * 0.5f - 120.0f, SCREEN_H * 0.5f + 120.0f, 240.0f, 66.0f };
static const Rectangle RESULT_PANEL = { SCREEN_W * 0.5f - 280.0f, SCREEN_H * 0.5f - 230.0f, 560.0f, 460.0f };

enum GameState { ST_MENU, ST_COUNTDOWN, ST_PLAYING, ST_GAMEOVER };

struct Ball {
    Vector2 pos;
    float radius;
    float life;
    float maxLife;
};

struct Fx {
    Vector2 pos;
    float age;
    float duration;
    float radius;
    bool  hit;
};

// ================= 全局状态 =================
static GameState state = ST_MENU;
static std::vector<Ball> balls;
static std::vector<Fx>   fxs;

static Sound sfxClick;
static Sound sfxHit;
static Sound sfxEmpty;
static Sound sfxCount;
static Sound sfxEnd;
static Texture2D texEagle;

static int   score = 0;
static int   hits  = 0;
static int   shots = 0;
static float timeLeft   = ROUND_TIME;
static float spawnTimer = 0.0f;

static float countTimer  = 0.0f;
static int   countNumber = 3;

static float recoil      = 0.0f;
static float muzzleFlash = 0.0f;
static float shakeTimer  = 0.0f;
static bool  paused      = false;

static Vector2 mousePos = { 0, 0 };

// ================= 工具函数 =================
static void DrawTextCentered(const char* text, float cx, float y, int fontSize, Color color) {
    int w = MeasureText(text, fontSize);
    DrawText(text, (int)(cx - w / 2.0f), (int)y, fontSize, color);
}

static bool PointInCircle(Vector2 p, Vector2 c, float r) {
    float dx = p.x - c.x;
    float dy = p.y - c.y;
    return dx * dx + dy * dy <= r * r;
}

static void ResetRound() {
    balls.clear();
    fxs.clear();
    score = hits = shots = 0;
    timeLeft = ROUND_TIME;
    spawnTimer = 0.15f;
    recoil = muzzleFlash = shakeTimer = 0.0f;
    paused = false;
    countTimer = 0.0f;
    countNumber = 3;
}

static void SpawnBall() {
    Ball b;
    b.radius = (float)GetRandomValue(16, 46);
    int m = (int)b.radius + 12;
    b.pos.x = (float)GetRandomValue(m, SCREEN_W - m);
    b.pos.y = (float)GetRandomValue(96 + m, SCREEN_H - 130 - m);
    b.maxLife = 2.4f;
    b.life    = b.maxLife;
    balls.push_back(b);
}

// ================= 背景 =================
static void DrawCheckerBackground(int ox, int oy) {
    ClearBackground(Color{ 30, 30, 36, 255 });
    const int cell = 64;

    // 交替色块，形成淡棋盘
    for (int gy = -1; gy <= SCREEN_H / cell + 1; ++gy) {
        for (int gx = -1; gx <= SCREEN_W / cell + 1; ++gx) {
            if ((gx + gy) & 1) {
                DrawRectangle(gx * cell + ox, gy * cell + oy, cell, cell, Color{ 34, 34, 41, 255 });
            }
        }
    }

    // 网格线
    Color line = Color{ 44, 44, 52, 255 };
    for (int x = -cell; x <= SCREEN_W + cell; x += cell)
        DrawLine(x + ox, 0, x + ox, SCREEN_H, line);
    for (int y = -cell; y <= SCREEN_H + cell; y += cell)
        DrawLine(0, y + oy, SCREEN_W, y + oy, line);
}

// ================= 沙鹰 =================
static Vector2 GetMuzzlePosition() {
    float fx = (mousePos.x - SCREEN_W * 0.5f) * 0.14f;
    float fy = (mousePos.y - SCREEN_H * 0.5f) * 0.06f;
    return Vector2{ SCREEN_W * 0.5f + fx, SCREEN_H - 176.0f + fy };
}

static void DrawDesertEagle(Vector2 muzzle, float rc) {
        float kick = rc * 26.0f;
        float targetH = SCREEN_H * 0.20f;
        float scale   = targetH / (float)texEagle.height;

        float drawX = muzzle.x - texEagle.width * scale * 0.5f;
        float drawY = muzzle.y + kick - texEagle.height * scale * 0.10f;

        Rectangle src = { 0, 0, (float)texEagle.width, (float)texEagle.height };
        Rectangle dst = { drawX, drawY,
                      texEagle.width * scale, texEagle.height * scale };
        Vector2 origin = { texEagle.width * scale * 0.5f, texEagle.height * scale * 0.10f };

        DrawTexturePro(texEagle, src, dst, origin, -rc * 6.0f, WHITE);
}

static void DrawMuzzleFlash(Vector2 muzzle, float rc, float k) {
    Vector2 fp = Vector2{ muzzle.x, muzzle.y + rc * 26.0f - 46.0f };
    DrawCircleV(fp, 26 * k, Fade(Color{ 255, 200, 90, 255 }, 0.85f));
    DrawCircleV(fp, 12 * k, Fade(WHITE, 0.95f));
    for (int i = 0; i < 4; ++i) {
        float ang = (float)i * (PI / 2.0f) + PI / 4.0f;
        Vector2 tip = Vector2{ fp.x + cosf(ang) * 52 * k, fp.y + sinf(ang) * 52 * k };
        DrawLineEx(fp, tip, 3.0f, Fade(Color{ 255, 235, 170, 255 }, 0.8f));
    }
}

// ================= HUD =================
static void DrawHUD() {
    DrawText(TextFormat("SCORE  %d", score), 24, 20, 28, RAYWHITE);
    DrawText(TextFormat("HITS   %d", hits),  24, 56, 22, Color{ 190, 200, 215, 255 });
    float acc = shots > 0 ? 100.0f * hits / shots : 0.0f;
    DrawText(TextFormat("ACC    %.1f%%", acc), 24, 86, 22, Color{ 190, 200, 215, 255 });

    DrawTextCentered(TextFormat("%.1f", timeLeft), SCREEN_W * 0.5f, 16, 34, RAYWHITE);
    DrawTextCentered("SECONDS LEFT", SCREEN_W * 0.5f, 56, 16, Color{ 150, 160, 175, 255 });

    float ratio = timeLeft / ROUND_TIME;
    Rectangle bg = { SCREEN_W * 0.5f - 160, 84, 320, 8 };
    DrawRectangleRec(bg, Color{ 50, 52, 60, 255 });
    Rectangle fg = { bg.x, bg.y, bg.width * ratio, bg.height };
    Color c = ratio > 0.3f ? Color{ 110, 190, 255, 255 } : Color{ 255, 110, 110, 255 };
    DrawRectangleRec(fg, c);
}

static void DrawPauseButton() {
    bool hover = CheckCollisionPointRec(mousePos, PAUSE_BTN);
    DrawRectangleRounded(PAUSE_BTN, 0.3f, 8, hover ? Color{ 70, 78, 92, 255 } : Color{ 48, 52, 62, 255 });
    float bw = 6, bh = 22, gap = 8;
    int cx = (int)(PAUSE_BTN.x + PAUSE_BTN.width * 0.5f);
    int cy = (int)(PAUSE_BTN.y + PAUSE_BTN.height * 0.5f);
    DrawRectangle(cx - (int)(gap * 0.5f) - (int)bw, cy - (int)(bh * 0.5f), (int)bw, (int)bh, RAYWHITE);
    DrawRectangle(cx + (int)(gap * 0.5f),          cy - (int)(bh * 0.5f), (int)bw, (int)bh, RAYWHITE);
}

static void DrawCrosshair(Vector2 m) {
    Color c = { 235, 245, 255, 235 };
    float gap = 7.0f, len = 13.0f;
    DrawLineEx(Vector2{ m.x - gap - len, m.y }, Vector2{ m.x - gap, m.y }, 2.0f, c);
    DrawLineEx(Vector2{ m.x + gap, m.y }, Vector2{ m.x + gap + len, m.y }, 2.0f, c);
    DrawLineEx(Vector2{ m.x, m.y - gap - len }, Vector2{ m.x, m.y - gap }, 2.0f, c);
    DrawLineEx(Vector2{ m.x, m.y + gap }, Vector2{ m.x, m.y + gap + len }, 2.0f, c);
    DrawCircleLines((int)m.x, (int)m.y, 2.0f, c);
}

// ================= 各状态覆盖层 =================
static void DrawMenu() {
    DrawTextCentered("AIM TRAINER", SCREEN_W * 0.5f, SCREEN_H * 0.5f - 220, 72, RAYWHITE);
    DrawTextCentered("DESERT EAGLE  -  120 SECONDS", SCREEN_W * 0.5f, SCREEN_H * 0.5f - 128, 22,
                     Color{ 150, 165, 185, 255 });

    Vector2 c = Vector2{ SCREEN_W * 0.5f, SCREEN_H * 0.5f + 30.0f };
    bool hover = PointInCircle(mousePos, c, 96.0f);
    DrawCircleV(c, hover ? 96.0f : 88.0f, hover ? Color{ 70, 140, 235, 255 } : Color{ 52, 108, 190, 255 });
    DrawCircleLines((int)c.x, (int)c.y, hover ? 96.0f : 88.0f, Color{ 140, 190, 255, 255 });
    DrawPoly(Vector2{ c.x + 10.0f, c.y }, 3, hover ? 46.0f : 42.0f, 0.0f, WHITE);

    DrawTextCentered("CLICK TO START", SCREEN_W * 0.5f, c.y + 130, 26, Color{ 180, 195, 215, 255 });
}

static void DrawCountdown() {
    float t = countTimer;
    float scale = 1.25f - 0.25f * t;
    unsigned char alpha = (unsigned char)(255 - 120 * t);
    const char* s = TextFormat("%d", countNumber);
    int fs = (int)(200 * scale);
    int w = MeasureText(s, fs);
    DrawText(s, SCREEN_W / 2 - w / 2, SCREEN_H / 2 - fs / 2, fs, Color{ 255, 255, 255, alpha });
}

static void DrawPauseOverlay() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{ 10, 10, 14, 255 }, 0.72f));
    DrawTextCentered("PAUSED", SCREEN_W * 0.5f, SCREEN_H * 0.5f - 96, 64, RAYWHITE);

    bool hover = CheckCollisionPointRec(mousePos, CONTINUE_BTN);
    DrawRectangleRounded(CONTINUE_BTN, 0.35f, 10, hover ? Color{ 90, 170, 255, 255 } : Color{ 60, 120, 210, 255 });
    DrawTextCentered("CONTINUE", CONTINUE_BTN.x + CONTINUE_BTN.width * 0.5f, CONTINUE_BTN.y + 17, 28, RAYWHITE);
}

static void DrawResult() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{ 8, 10, 16, 255 }, 0.72f));

    Rectangle p = RESULT_PANEL;
    DrawRectangleRounded(p, 0.06f, 12, Color{ 34, 38, 46, 255 });
    DrawTextCentered("ROUND OVER", p.x + p.width * 0.5f, p.y + 34, 52, RAYWHITE);

    float acc = shots > 0 ? 100.0f * hits / shots : 0.0f;

    float y0 = p.y + 130;
    auto row = [&](const char* label, const char* value, float y) {
        DrawText(label, (int)(p.x + 60), (int)y, 26, Color{ 170, 182, 200, 255 });
        int vw = MeasureText(value, 30);
        DrawText(value, (int)(p.x + p.width - 60 - vw), (int)y - 2, 30, RAYWHITE);
    };
    row("HITS",     TextFormat("%d", hits),  y0);
    row("SHOTS",    TextFormat("%d", shots), y0 + 52);
    row("ACCURACY", TextFormat("%.1f%%", acc), y0 + 104);
    row("SCORE",    TextFormat("%d", score), y0 + 156);

    bool hover = CheckCollisionPointRec(mousePos, RESTART_BTN);
    DrawRectangleRounded(RESTART_BTN, 0.35f, 12, hover ? Color{ 96, 172, 255, 255 } : Color{ 62, 124, 210, 255 });
    DrawTextCentered("RESTART", RESTART_BTN.x + RESTART_BTN.width * 0.5f, RESTART_BTN.y + 18, 28, WHITE);
}

// ================= 主函数 =================
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Aim Trainer - Desert Eagle");
    InitAudioDevice();
    sfxClick = LoadSound("assets/click.wav");
    sfxHit   = LoadSound("assets/hit.wav");
    sfxEmpty = LoadSound("assets/empty.wav");
    sfxCount = LoadSound("assets/count.wav");
    sfxEnd   = LoadSound("assets/end.wav");
    texEagle = LoadTexture("assets/eagle.png");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(nullptr));
    HideCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;
        mousePos = GetMousePosition();

        // ---------------- 更新 ----------------
        switch (state) {
        case ST_MENU: {
            Vector2 c = Vector2{ SCREEN_W * 0.5f, SCREEN_H * 0.5f + 30.0f };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && PointInCircle(mousePos, c, 96.0f)) {
                ResetRound();
                state = ST_COUNTDOWN;
            }
        } break;

        case ST_COUNTDOWN: {
            countTimer += dt;
            if (countTimer >= 1.0f) {
                countTimer -= 1.0f;
                countNumber--;
                PlaySound(sfxCount);
                if (countNumber <= 0) {
                    state = ST_PLAYING;
                    timeLeft = ROUND_TIME;
                    spawnTimer = 0.15f;
                }
            }
        } break;

        case ST_PLAYING: {
            if (paused) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, CONTINUE_BTN))
                    paused = false;
                break;
            }

            timeLeft -= dt;
            if (timeLeft <= 0.0f) {
                timeLeft = 0.0f;
                state = ST_GAMEOVER;
                PlaySound(sfxEnd);
                balls.clear();
                break;
            }

            spawnTimer -= dt;
            if (spawnTimer <= 0.0f) {
                spawnTimer = 0.32f;
                if ((int)balls.size() < MAX_BALLS) SpawnBall();
            }

            for (auto& b : balls) b.life -= dt;
            balls.erase(std::remove_if(balls.begin(), balls.end(),
                [](const Ball& b) { return b.life <= 0.0f; }), balls.end());

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mousePos, PAUSE_BTN)) {
                    paused = true;
                } else {
                    shots++;
                    recoil = 1.0f;
                    muzzleFlash = 1.0f;
                    PlaySound(sfxClick);
                    bool hitSomething = false;

                    for (int i = (int)balls.size() - 1; i >= 0; --i) {
                        if (PointInCircle(mousePos, balls[i].pos, balls[i].radius)) {
                            hitSomething = true;
                            PlaySound(sfxHit);
                            hits++;
                            score += (int)(900.0f / balls[i].radius) + 5;

                            Fx f;
                            f.pos = balls[i].pos;
                            f.age = 0.0f;
                            f.duration = 0.32f;
                            f.radius = balls[i].radius;
                            f.hit = true;
                            fxs.push_back(f);

                            balls.erase(balls.begin() + i);
                            shakeTimer = 0.06f;
                            break;
                        }
                    }

                    if (!hitSomething) {
                        PlaySound(sfxEmpty);
                        Fx f;
                        f.pos = mousePos;
                        f.age = 0.0f;
                        f.duration = 0.22f;
                        f.radius = 8.0f;
                        f.hit = false;
                        fxs.push_back(f);
                    }
                }
            }

            recoil      = std::max(0.0f, recoil - dt * 6.0f);
            muzzleFlash = std::max(0.0f, muzzleFlash - dt * 14.0f);
            shakeTimer  = std::max(0.0f, shakeTimer - dt);
        } break;

        case ST_GAMEOVER: {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, RESTART_BTN)) {
                ResetRound();
                state = ST_COUNTDOWN;
            }
        } break;
        }

        // 特效衰减（所有状态通用）
        for (auto& f : fxs) f.age += dt;
        fxs.erase(std::remove_if(fxs.begin(), fxs.end(),
            [](const Fx& f) { return f.age >= f.duration; }), fxs.end());

       // ---------------- 绘制 ----------------
        BeginDrawing();

        int sx = 0, sy = 0;
        if (shakeTimer > 0.0f) {
            sx = GetRandomValue(-3, 3);
            sy = GetRandomValue(-3, 3);
        }
        DrawCheckerBackground(sx, sy);

        // 小球
        for (auto& b : balls) {
            float k = b.life / b.maxLife;
            unsigned char a = (unsigned char)(90 + 165 * k);
            DrawCircleV(b.pos, b.radius, Color{ 130, 200, 255, a });
            DrawCircleLines((int)b.pos.x, (int)b.pos.y, b.radius, Color{ 190, 225, 255, a });
            DrawCircleV(Vector2{ b.pos.x - b.radius * 0.3f, b.pos.y - b.radius * 0.3f },
                        b.radius * 0.18f, Color{ 235, 248, 255, a });
        }

        // 命中/点空特效
        for (auto& f : fxs) {
            float t = f.age / f.duration;
            float r = f.radius * (0.6f + 1.8f * t);
            unsigned char a = (unsigned char)(220 * (1.0f - t));
            Color c = f.hit ? Color{ 150, 220, 255, a } : Color{ 255, 240, 200, a };
            DrawRing(f.pos, r * 0.85f, r, 0, 360, 40, c);
        }

        // 枪
        if (state == ST_PLAYING || state == ST_COUNTDOWN) {
            Vector2 muzzle = GetMuzzlePosition();
            DrawDesertEagle(muzzle, recoil);
            if (muzzleFlash > 0.0f) DrawMuzzleFlash(muzzle, recoil, muzzleFlash);
        }

        // HUD
        if (state == ST_PLAYING || state == ST_COUNTDOWN || state == ST_GAMEOVER) DrawHUD();

        // 各状态覆盖层
        if (state == ST_MENU) {
            DrawMenu();
        } else if (state == ST_COUNTDOWN) {
            DrawCountdown();
        } else if (state == ST_PLAYING) {
            if (paused) DrawPauseOverlay();
            else        DrawPauseButton();
        } else if (state == ST_GAMEOVER) {
            DrawResult();
        }

        DrawCrosshair(mousePos);
        DrawFPS(SCREEN_W - 100, SCREEN_H - 30);

        EndDrawing();
    }

    UnloadSound(sfxClick);
    UnloadSound(sfxHit);
    UnloadSound(sfxEmpty);
    UnloadSound(sfxCount);
    UnloadSound(sfxEnd);
    UnloadTexture(texEagle);
    CloseAudioDevice();

    CloseWindow();
    return 0;
}
