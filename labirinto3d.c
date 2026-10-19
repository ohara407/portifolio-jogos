#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT };

#ifdef _WIN32
#include <windows.h>
#include <conio.h>

static void term_init(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}

static void term_restore(void) {}

static int read_key(void)
{
    if (!_kbhit()) return -1;
    int c = _getch();
    if (c == 0 || c == 224) {
        switch (_getch()) {
        case 72: return K_UP;
        case 80: return K_DOWN;
        case 75: return K_LEFT;
        case 77: return K_RIGHT;
        }
        return -1;
    }
    return c;
}

static void sleep_ms(int ms) { Sleep(ms); }
static double now_s(void) { return GetTickCount64() / 1000.0; }

#else
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>

static struct termios saved_tio;

static void term_init(void)
{
    tcgetattr(0, &saved_tio);
    struct termios t = saved_tio;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
}

static void term_restore(void) { tcsetattr(0, TCSANOW, &saved_tio); }

static int read_key(void)
{
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    if (c != 27) return c;
    unsigned char seq[2];
    if (read(0, &seq[0], 1) != 1) return 27;
    if (read(0, &seq[1], 1) != 1) return 27;
    if (seq[0] != '[') return -1;
    switch (seq[1]) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'D': return K_LEFT;
    case 'C': return K_RIGHT;
    }
    return -1;
}

static void sleep_ms(int ms) { usleep(ms * 1000); }

static double now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}
#endif

#define MAP_W 16
#define MAP_H 16
#define VIEW_W 80
#define VIEW_H 42
#define MAX_GEMS 8

enum { ST_MENU, ST_PLAY, ST_WIN };

static const char *maze[MAP_H] = {
    "################",
    "#......#.......#",
    "#.@@.#.#.$$$$..#",
    "#.@..#...$..C..#",
    "#.@.####.$.$$..#",
    "#...#....$.....#",
    "###.#.####.###.#",
    "#...#.#......#.#",
    "#.###.#.@@@@.#.#",
    "#.#C..#.@..@...#",
    "#.#####.@.@@.###",
    "#.......@....#E#",
    "#.###.#####.##.#",
    "#...#.....#..C.#",
    "#.#...###...#..#",
    "################",
};

static const int RAMP_STONE[6] = { 153, 110, 67, 60, 59, 236 };
static const int RAMP_BRICK[6] = { 210, 167, 131, 95, 52, 234 };
static const int RAMP_MOSS[6]  = { 150, 107, 71, 65, 22, 234 };
static const int RAMP_DOOR[6]  = { 220, 214, 172, 136, 94, 234 };
static const int RAMP_OPEN[6]  = { 121, 84, 41, 35, 29, 234 };
static const int SKY[7] = { 233, 17, 17, 18, 18, 24, 24 };
static const int FLOOR[6] = { 235, 236, 237, 238, 239, 240 };

typedef struct { double x, y; int taken; } Gem;

static Gem gems[MAX_GEMS];
static int ngems, got;
static double px, py, angle;
static double start_time, final_time, best_time;
static int state = ST_MENU;
static int show_map = 1;
static int running = 1;

static unsigned char pix[VIEW_H][VIEW_W];
static double zbuf[VIEW_W];

static char frame[1 << 17];
static int flen;
static int cur_fg, cur_bg;

static char cell(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return '#';
    return maze[y][x];
}

static int door_open(void) { return got == ngems; }

static int solid(int x, int y)
{
    char c = cell(x, y);
    if (c == 'E') return !door_open();
    return c == '#' || c == '@' || c == '$';
}

static void reset(void)
{
    ngems = 0;
    got = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (maze[y][x] == 'C' && ngems < MAX_GEMS) {
                gems[ngems].x = x + 0.5;
                gems[ngems].y = y + 0.5;
                gems[ngems].taken = 0;
                ngems++;
            }
    px = 1.5;
    py = 1.5;
    angle = 0.0;
}

static int shade(const int *ramp, double dist, int dark)
{
    int i = (int)(dist / 2.2) + dark;
    if (i > 5) i = 5;
    return ramp[i];
}

static double frac(double v) { return v - floor(v); }

static void cast(double t)
{
    double dir_x = cos(angle), dir_y = sin(angle);
    double plane_x = -dir_y * 0.66, plane_y = dir_x * 0.66;
    double scale = VIEW_W * 0.5 / 0.66;
    int horizon = VIEW_H / 2;

    for (int y = 0; y < VIEW_H; y++) {
        for (int x = 0; x < VIEW_W; x++) {
            if (y < horizon) {
                int i = y * 7 / horizon;
                pix[y][x] = (unsigned char)SKY[i];
            } else {
                int i = (y - horizon) * 6 / (VIEW_H - horizon);
                pix[y][x] = (unsigned char)FLOOR[i];
            }
        }
    }

    for (int x = 0; x < VIEW_W; x++) {
        double cam = 2.0 * x / VIEW_W - 1.0;
        double rx = dir_x + plane_x * cam, ry = dir_y + plane_y * cam;
        int mx = (int)px, my = (int)py;
        double ddx = rx == 0 ? 1e30 : fabs(1.0 / rx);
        double ddy = ry == 0 ? 1e30 : fabs(1.0 / ry);
        int step_x = rx < 0 ? -1 : 1, step_y = ry < 0 ? -1 : 1;
        double side_x = rx < 0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
        double side_y = ry < 0 ? (py - my) * ddy : (my + 1.0 - py) * ddy;
        int side = 0;

        for (int guard = 0; guard < 64; guard++) {
            if (side_x < side_y) { side_x += ddx; mx += step_x; side = 0; }
            else { side_y += ddy; my += step_y; side = 1; }
            if (solid(mx, my) || cell(mx, my) == 'E') break;
        }

        double dist = side == 0 ? side_x - ddx : side_y - ddy;
        if (dist < 0.05) dist = 0.05;
        zbuf[x] = dist;

        double wall_x = side == 0 ? py + dist * ry : px + dist * rx;
        wall_x = frac(wall_x);
        char kind = cell(mx, my);
        const int *ramp = kind == '@' ? RAMP_BRICK : kind == '$' ? RAMP_MOSS : RAMP_STONE;
        if (kind == 'E') ramp = door_open() ? RAMP_OPEN : RAMP_DOOR;

        int height = (int)(scale / dist);
        int top = horizon - height / 2, bottom = horizon + height / 2;

        for (int y = top < 0 ? 0 : top; y < VIEW_H && y < bottom; y++) {
            double v = (double)(y - top) / (height ? height : 1);
            int dark = side;
            if (kind == '@') {
                int row = (int)(v * 5);
                if (frac(v * 5) < 0.14 || frac(wall_x * 2 + (row % 2) * 0.5) < 0.07) dark += 2;
            } else if (kind == '$') {
                if (sin(wall_x * 19 + v * 7) > 0.75) dark += 1;
            } else if (kind == 'E') {
                if (wall_x < 0.08 || wall_x > 0.92 || v < 0.06) dark += 2;
                if (door_open() && sin(v * 20 - t * 6) > 0.6) dark = 0;
            } else if (wall_x < 0.03 || wall_x > 0.97) {
                dark += 1;
            }
            pix[y][x] = (unsigned char)shade(ramp, dist, dark);
        }
    }

    double inv = 1.0 / (plane_x * dir_y - dir_x * plane_y);
    int order[MAX_GEMS];
    double dists[MAX_GEMS];
    for (int i = 0; i < ngems; i++) {
        order[i] = i;
        dists[i] = (gems[i].x - px) * (gems[i].x - px) + (gems[i].y - py) * (gems[i].y - py);
    }
    for (int i = 0; i < ngems; i++)
        for (int j = i + 1; j < ngems; j++)
            if (dists[order[j]] > dists[order[i]]) { int tmp = order[i]; order[i] = order[j]; order[j] = tmp; }

    for (int n = 0; n < ngems; n++) {
        Gem *g = &gems[order[n]];
        if (g->taken) continue;
        double sx = g->x - px, sy = g->y - py;
        double tx = inv * (dir_y * sx - dir_x * sy);
        double ty = inv * (-plane_y * sx + plane_x * sy);
        if (ty <= 0.2) continue;
        int screen_x = (int)(VIEW_W / 2 * (1 + tx / ty));
        int size = (int)(scale / ty * 0.45);
        int cy = horizon + (int)(scale / ty * 0.12 + sin(t * 3 + g->x) * scale / ty * 0.05);
        int half = size / 2 > 0 ? size / 2 : 1;
        for (int x = screen_x - half; x <= screen_x + half; x++) {
            if (x < 0 || x >= VIEW_W || ty >= zbuf[x]) continue;
            double u = (double)(x - screen_x) / half;
            for (int y = cy - size; y <= cy + size; y++) {
                if (y < 0 || y >= VIEW_H) continue;
                double v = (double)(y - cy) / size;
                if (fabs(u) + fabs(v) > 1.0) continue;
                int c = v < -0.2 ? (u < 0 ? 159 : 123) : (u < 0 ? 51 : 44);
                if (ty > 6) c = 37;
                pix[y][x] = (unsigned char)c;
            }
        }
    }

    if (show_map) {
        for (int my = 0; my < MAP_H; my++) {
            for (int mx = 0; mx < MAP_W; mx++) {
                char c = maze[my][mx];
                int col = 233;
                if (c == '#' || c == '@' || c == '$') col = 245;
                if (c == 'E') col = door_open() ? 46 : 208;
                pix[my + 1][mx + 1] = (unsigned char)col;
            }
        }
        for (int i = 0; i < ngems; i++)
            if (!gems[i].taken) pix[(int)gems[i].y + 1][(int)gems[i].x + 1] = 51;
        pix[(int)py + 1][(int)px + 1] = 226;
        int fx = (int)(px + cos(angle) * 1.2), fy = (int)(py + sin(angle) * 1.2);
        if (fx >= 0 && fy >= 0 && fx < MAP_W && fy < MAP_H && pix[fy + 1][fx + 1] == 233) pix[fy + 1][fx + 1] = 178;
    }
}

static void emit(const char *s)
{
    size_t n = strlen(s);
    if (flen + (int)n >= (int)sizeof(frame)) return;
    memcpy(frame + flen, s, n);
    flen += (int)n;
}

static void color(int fg, int bg)
{
    char tmp[24];
    if (fg != cur_fg) {
        snprintf(tmp, sizeof tmp, "\x1b[38;5;%dm", fg);
        emit(tmp);
        cur_fg = fg;
    }
    if (bg != cur_bg) {
        snprintf(tmp, sizeof tmp, "\x1b[48;5;%dm", bg);
        emit(tmp);
        cur_bg = bg;
    }
}

static void plain(void)
{
    emit("\x1b[0m");
    cur_fg = cur_bg = -1;
}

static void render(double t)
{
    char tmp[160];
    cast(t);
    flen = 0;
    cur_fg = cur_bg = -1;
    emit("\x1b[H");
    for (int r = 0; r < VIEW_H / 2; r++) {
        for (int x = 0; x < VIEW_W; x++) {
            int top = pix[r * 2][x], bot = pix[r * 2 + 1][x];
            if (top == bot) {
                color(cur_fg < 0 ? 0 : cur_fg, top);
                emit(" ");
            } else {
                color(top, bot);
                emit("▀");
            }
        }
        plain();
        emit("\r\n");
    }

    double elapsed = state == ST_PLAY ? t - start_time : final_time;
    emit("\x1b[1;38;5;51m Cristais ");
    snprintf(tmp, sizeof tmp, "%d/%d", got, ngems);
    emit(tmp);
    emit("\x1b[0m   ");
    snprintf(tmp, sizeof tmp, "Tempo %.1fs", state == ST_MENU ? 0.0 : elapsed);
    emit(tmp);
    if (best_time > 0) {
        snprintf(tmp, sizeof tmp, "   Recorde %.1fs", best_time);
        emit(tmp);
    }
    emit(door_open() ? "   \x1b[1;38;5;46mA saída abriu!" : "   \x1b[38;5;208mSaída trancada");
    plain();
    emit("\x1b[K\r\n");

    switch (state) {
    case ST_MENU:
        emit("\x1b[1;38;5;226m LABIRINTO 3D  \x1b[0mAche os 3 cristais e saia. ENTER para começar.");
        break;
    case ST_WIN:
        snprintf(tmp, sizeof tmp, "\x1b[1;38;5;46m Você escapou em %.1fs!\x1b[0m  ENTER joga de novo, X sai.", final_time);
        emit(tmp);
        break;
    default:
        emit("\x1b[38;5;245m W/S andar  A/D girar  Q/E passo lateral  M mapa  X sair");
    }
    plain();
    emit("\x1b[K");
    fwrite(frame, 1, flen, stdout);
    fflush(stdout);
}

static void try_move(double dx, double dy)
{
    const double r = 0.22;
    double nx = px + dx, ny = py + dy;
    if (!solid((int)(nx + (dx > 0 ? r : -r)), (int)py)) px = nx;
    if (!solid((int)px, (int)(ny + (dy > 0 ? r : -r)))) py = ny;
}

static void update(double t)
{
    for (int i = 0; i < ngems; i++) {
        Gem *g = &gems[i];
        if (!g->taken && fabs(g->x - px) < 0.6 && fabs(g->y - py) < 0.6) {
            g->taken = 1;
            got++;
        }
    }
    if (cell((int)px, (int)py) == 'E' && door_open()) {
        state = ST_WIN;
        final_time = t - start_time;
        if (best_time == 0 || final_time < best_time) best_time = final_time;
    }
}

static void handle_key(int k, double t)
{
    if (k == 'x' || k == 'X') { running = 0; return; }
    if (k == 'm' || k == 'M') { show_map = !show_map; return; }
    if (state != ST_PLAY) {
        if (k == '\n' || k == '\r' || k == ' ') {
            reset();
            state = ST_PLAY;
            start_time = t;
        }
        return;
    }
    const double step = 0.18, turn = 0.11;
    double cx = cos(angle), sy = sin(angle);
    switch (k) {
    case 'w': case 'W': case K_UP: try_move(cx * step, sy * step); break;
    case 's': case 'S': case K_DOWN: try_move(-cx * step, -sy * step); break;
    case 'a': case 'A': case K_LEFT: angle -= turn; break;
    case 'd': case 'D': case K_RIGHT: angle += turn; break;
    case 'q': case 'Q': try_move(sy * step, -cx * step); break;
    case 'e': case 'E': try_move(-sy * step, cx * step); break;
    }
}

static void cleanup(void)
{
    printf("\x1b[0m\x1b[?25h\x1b[?1049l");
    fflush(stdout);
    term_restore();
}

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    term_init();
    atexit(cleanup);
    signal(SIGINT, on_signal);
    printf("\x1b[?1049h\x1b[?25l\x1b[2J");
    reset();

    double last = now_s();
    while (running) {
        double t = now_s();
        double dt = t - last;
        last = t;
        int k;
        while ((k = read_key()) != -1) handle_key(k, t);
        if (state == ST_MENU) angle += dt * 0.4;
        if (state == ST_PLAY) update(t);
        render(t);
        sleep_ms(33);
    }
    return 0;
}
