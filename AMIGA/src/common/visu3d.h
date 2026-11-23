/* Amiga port of Future Crew's VISU vector engine (used by U2A and U2E). */
#ifndef VISU3D_H
#define VISU3D_H
#include "kernel.h"

struct v3obj {
    u16 nv;                 /* vertices */
    u16 nl;                 /* polygon lists (0 = unsorted, 1.. = sorted) */
    u16 center;             /* center vertex (for object distance) */
    const s16 *v;           /* vertices x,y,z (scaled by 1/8) */
    const s16 *n;           /* face normals x,y,z (unit 16384) */
    const u16 *pd;          /* polygons: sides|flags<<8, color, normal, verts... */
    const u16 *const *lists;/* sortvertex, count, polygon offsets... */
};

#define V3_MAXINST 64
#define V3_MAXV 320

struct v3scene {
    const struct v3obj *objs;
    const u8 *inst;
    int conum;
    const u8 *const *streams;
    int nstreams;
    char kind;
    const u8 *colmap;
    const u8 *iflags;       /* optional U2E sort flags per instance */
    /* animation state */
    s32 frame;              /* animation frames done */
    int si;
    const u8 *sp;
    int fov;
    int wrapped;
    s32 r0[V3_MAXINST][12];
    u8 on[V3_MAXINST];
};

struct v3screen {
    u8 *planes[5];          /* destination bitplanes (40 bytes per row) */
    u8 *scratch;            /* one plane, same size, must be clear */
    u8 *scratch2;           /* a second one (polygons alternate) */
    int h;                  /* rows */
    int addx, addy;         /* projection centre */
    int clipy0, clipy1;     /* clip rows [y0, y1) */
};

void v3_init(struct v3scene *s);
void v3_step(struct v3scene *s);        /* advance one animation frame */
void v3_draw(struct v3scene *s, struct v3screen *scr);
void v3_fill(struct v3screen *scr, int n, const s16 *xs, const s16 *ys, int color);
extern int v3_polys_drawn;

#endif
