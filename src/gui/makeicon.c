/*  Builds icon.ico for RomesCov: a dark rounded tile with the survey track across it,
 *  green where there is coverage and red where the run lost it.  Everything is drawn
 *  analytically, so every size comes out sharp and antialiased.
 *
 *  gcc -std=c99 -O2 makeicon.c -o makeicon.exe -lm && makeicon.exe icon.ico
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const int sizes[] = { 16, 32, 48, 64, 128 };
#define SIZE_COUNT ((int)(sizeof(sizes) / sizeof(sizes[0])))

/* The track, in fractions of the tile: two segments in the middle are drawn red, the
   rest green - the picture the map draws, boiled down to eight points. */
static const double track[][2] = {
    { 0.13, 0.80 }, { 0.25, 0.67 }, { 0.35, 0.63 }, { 0.45, 0.53 },
    { 0.56, 0.47 }, { 0.66, 0.36 }, { 0.76, 0.31 }, { 0.88, 0.19 }
};
#define TRACK_POINTS ((int)(sizeof(track) / sizeof(track[0])))
#define GAP_FROM 3      /* two segments in the middle are the hole in the coverage */
#define GAP_COUNT 2

typedef struct { double r, g, b; } colour_t;

static const colour_t tile_colour  = { 0.086, 0.129, 0.196 };   /* dark slate */
static const colour_t green_colour = { 0.157, 0.784, 0.471 };
static const colour_t red_colour   = { 0.839, 0.255, 0.216 };
static const colour_t grid_colour  = { 0.204, 0.271, 0.365 };

static double clamp01(double value)
{
    if(value < 0.0) return 0.0;
    if(value > 1.0) return 1.0;
    return value;
}

/*----Distance from a point to a segment.----*/
static double segment_distance(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax;
    double dy = by - ay;
    double length = dx * dx + dy * dy;
    double t;

    if(length < 1e-12) return sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
    t = ((px - ax) * dx + (py - ay) * dy) / length;
    t = clamp01(t);
    dx = px - (ax + t * dx);
    dy = py - (ay + t * dy);
    return sqrt(dx * dx + dy * dy);
}

/*----Signed distance to a rounded rectangle: negative inside.----*/
static double tile_distance(double px, double py, double half, double radius)
{
    double dx = fabs(px) - (half - radius);
    double dy = fabs(py) - (half - radius);
    double ax = (dx > 0.0) ? dx : 0.0;
    double ay = (dy > 0.0) ? dy : 0.0;
    double outside = sqrt(ax * ax + ay * ay);
    double inside = (dx > dy) ? dx : dy;

    if(inside > 0.0) inside = 0.0;
    return outside + inside - radius;
}

static void blend(colour_t* base, colour_t over, double alpha)
{
    base->r = base->r * (1.0 - alpha) + over.r * alpha;
    base->g = base->g * (1.0 - alpha) + over.g * alpha;
    base->b = base->b * (1.0 - alpha) + over.b * alpha;
}

/*----Draws one size into a BGRA buffer, bottom-up as an icon wants it.----*/
static void render(int size, unsigned char* pixels)
{
    double centre = size / 2.0;
    double radius = size * 0.20;
    double pen    = size * 0.072;          /* half width of the track */
    double grid   = size * 0.018;
    int    x, y, i;

    for(y = 0; y < size; y++)
    {
        for(x = 0; x < size; x++)
        {
            double px = x + 0.5;
            double py = y + 0.5;
            double tile = tile_distance(px - centre, py - centre, centre, radius);
            double alpha = clamp01(0.5 - tile);
            colour_t colour = tile_colour;
            double distance;

            if(alpha > 0.0)
            {
                /* two faint grid lines, dropped on the small sizes where they only blur */
                if(size >= 32)
                {
                    double gx = fabs(px - size * 0.36);
                    double gy = fabs(py - size * 0.63);
                    double near = (gx < gy) ? gx : gy;
                    blend(&colour, grid_colour, clamp01(grid + 0.5 - near) * 0.6);
                }

                for(i = 0; i + 1 < TRACK_POINTS; i++)
                {
                    if((i >= GAP_FROM) && (i < GAP_FROM + GAP_COUNT)) continue;
                    distance = segment_distance(px, py,
                                                track[i][0] * size, track[i][1] * size,
                                                track[i + 1][0] * size, track[i + 1][1] * size);
                    blend(&colour, green_colour, clamp01(pen + 0.5 - distance));
                }

                for(i = GAP_FROM; i < GAP_FROM + GAP_COUNT; i++)
                {
                    distance = segment_distance(px, py,
                                                track[i][0] * size, track[i][1] * size,
                                                track[i + 1][0] * size, track[i + 1][1] * size);
                    blend(&colour, red_colour, clamp01(pen + 0.5 - distance));
                }
            }

            {
                int row = (size - 1 - y) * size * 4;   /* icons are stored bottom-up */
                unsigned char* pixel = pixels + row + x * 4;
                pixel[0] = (unsigned char)(clamp01(colour.b) * 255.0 + 0.5);
                pixel[1] = (unsigned char)(clamp01(colour.g) * 255.0 + 0.5);
                pixel[2] = (unsigned char)(clamp01(colour.r) * 255.0 + 0.5);
                pixel[3] = (unsigned char)(alpha * 255.0 + 0.5);
            }
        }
    }
}

static void put16(unsigned char* p, unsigned value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
}

static void put32(unsigned char* p, unsigned value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
    p[2] = (unsigned char)((value >> 16) & 0xFF);
    p[3] = (unsigned char)((value >> 24) & 0xFF);
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "icon.ico";
    FILE*  file = fopen(path, "wb");
    unsigned char header[6];
    unsigned char entry[16];
    unsigned char info[40];
    unsigned offset;
    int i;

    if(file == NULL) { perror(path); return 1; }

    put16(header + 0, 0);
    put16(header + 2, 1);
    put16(header + 4, SIZE_COUNT);
    fwrite(header, 1, sizeof(header), file);

    offset = 6 + 16 * SIZE_COUNT;
    for(i = 0; i < SIZE_COUNT; i++)
    {
        int size = sizes[i];
        int mask_row = ((size + 31) / 32) * 4;
        unsigned bytes = 40 + (unsigned)(size * size * 4) + (unsigned)(mask_row * size);

        memset(entry, 0, sizeof(entry));
        entry[0] = (unsigned char)((size >= 256) ? 0 : size);
        entry[1] = (unsigned char)((size >= 256) ? 0 : size);
        put16(entry + 4, 1);
        put16(entry + 6, 32);
        put32(entry + 8, bytes);
        put32(entry + 12, offset);
        fwrite(entry, 1, sizeof(entry), file);
        offset += bytes;
    }

    for(i = 0; i < SIZE_COUNT; i++)
    {
        int size = sizes[i];
        int mask_row = ((size + 31) / 32) * 4;
        unsigned char* pixels = (unsigned char*)calloc((size_t)size * size * 4, 1);
        unsigned char* mask = (unsigned char*)calloc((size_t)mask_row * size, 1);

        if((pixels == NULL) || (mask == NULL)) { fprintf(stderr, "out of memory\n"); return 1; }
        render(size, pixels);

        memset(info, 0, sizeof(info));
        put32(info + 0, 40);
        put32(info + 4, (unsigned)size);
        put32(info + 8, (unsigned)(size * 2));      /* colours plus mask, as the format wants */
        put16(info + 12, 1);
        put16(info + 14, 32);
        put32(info + 20, (unsigned)(size * size * 4 + mask_row * size));
        fwrite(info, 1, sizeof(info), file);
        fwrite(pixels, 1, (size_t)size * size * 4, file);
        fwrite(mask, 1, (size_t)mask_row * size, file);

        free(pixels);
        free(mask);
    }

    fclose(file);
    printf("%s written, %d sizes\n", path, SIZE_COUNT);
    return 0;
}
