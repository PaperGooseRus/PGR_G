#pragma once
/*
 * Только для headless-проверки компиляции клиентского кода без raylib.
 * Не подключать при сборке настоящей игры. Stub возвращает один кадр.
 */
#include <cstdarg>
#include <cstdio>
#include <cstring>

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Color { unsigned char r, g, b, a; };
struct Texture2D { unsigned id; };
struct Font { Texture2D texture; };
struct Camera3D { Vector3 position, target, up; float fovy; int projection; };
struct Ray { Vector3 position, direction; };
struct BoundingBox { Vector3 min, max; };
struct RayCollision { bool hit; float distance; Vector3 point, normal; };

constexpr int FLAG_MSAA_4X_HINT = 1;
constexpr int FLAG_WINDOW_RESIZABLE = 2;
constexpr int CAMERA_PERSPECTIVE = 0;
constexpr int KEY_TAB = 1;
constexpr int KEY_F1 = 2;
constexpr int KEY_W = 3;
constexpr int KEY_A = 4;
constexpr int KEY_S = 5;
constexpr int KEY_D = 6;
constexpr int KEY_LEFT_SHIFT = 7;
constexpr int KEY_RIGHT_SHIFT = 8;
constexpr int KEY_SPACE = 9;
constexpr int KEY_E = 10;
constexpr int KEY_I = 11;
constexpr int KEY_J = 12;
constexpr int KEY_T = 13;
constexpr int KEY_C = 14;
constexpr int KEY_P = 15;
constexpr int KEY_R = 16;
constexpr Color RAYWHITE{245, 245, 245, 255};
constexpr Color GRAY{130, 130, 130, 255};
constexpr Color LIGHTGRAY{200, 200, 200, 255};
constexpr Color YELLOW{253, 249, 0, 255};

inline void SetConfigFlags(unsigned) {}
inline void InitWindow(int, int, const char*) {}
inline void SetWindowMinSize(int, int) {}
inline void SetTargetFPS(int) {}
inline void DisableCursor() {}
inline void EnableCursor() {}
inline bool WindowShouldClose() { static int frames = 0; return frames++ >= 1; }
inline float GetFrameTime() { return 0.016f; }
inline bool IsKeyPressed(int) { return false; }
inline bool IsKeyDown(int) { return false; }
inline bool IsWindowFocused() { return true; }
inline Vector2 GetMouseDelta() { return {0.0f, 0.0f}; }
inline float GetMouseWheelMove() { return 0.0f; }
inline RayCollision GetRayCollisionBox(Ray, BoundingBox) {
    return {false, 0.0f, {}, {}};
}
inline void BeginDrawing() {}
inline void ClearBackground(Color) {}
inline void BeginMode3D(Camera3D) {}
inline void EndMode3D() {}
inline void EndDrawing() {}
inline void CloseWindow() {}
inline void DrawCube(Vector3, float, float, float, Color) {}
inline void DrawCubeWires(Vector3, float, float, float, Color) {}
inline void DrawGrid(int, float) {}
inline Vector2 GetWorldToScreen(Vector3, Camera3D) { return {200.0f, 200.0f}; }
inline int MeasureText(const char* text, int fontSize) {
    return static_cast<int>(std::strlen(text)) * fontSize / 2;
}
inline void DrawRectangle(int, int, int, int, Color) {}
inline void DrawRectangleLines(int, int, int, int, Color) {}
inline void DrawText(const char*, int, int, int, Color) {}
inline void DrawFPS(int, int) {}
inline int GetScreenWidth() { return 1280; }
inline int GetScreenHeight() { return 720; }
inline const char* TextFormat(const char* fmt, ...) {
    static char message[1024];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    return message;
}

inline bool FileExists(const char*) {return true;}
inline Font LoadFontEx(const char*,int,int*,int) {return {{1}};}
inline void UnloadFont(Font) {}
inline void DrawTextEx(Font,const char*,Vector2,float,float,Color) {}
inline Vector2 MeasureTextEx(Font,const char* text,float size,float) {
    return {static_cast<float>(std::strlen(text))*size*0.46f,size};
}
