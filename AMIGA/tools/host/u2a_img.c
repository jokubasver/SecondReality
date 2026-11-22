#include "kernel_blit.h"
#include "visu3d.c"
#include "u2a_scene.h"
static u8 pl[5][6000 + 80], scratch[6080 + 80];
int main(int argc, char **argv)
{
    static struct v3scene sc;
    struct v3screen scr;
    int f, i, from = atoi(argv[1]), to = atoi(argv[2]), every = atoi(argv[3]);
    sc.objs = u2a_objs; sc.inst = u2a_inst; sc.conum = U2A_CONUM; sc.streams = u2a_streams;
    sc.nstreams = U2A_NSTREAMS; sc.kind = U2A_KIND; sc.colmap = u2a_colmap; sc.iflags = u2a_iflags;
    v3_init(&sc); v3_step(&sc);
    scr.scratch = scratch; scr.h = 150; scr.addx = 159; scr.addy = 74; scr.clipy0 = 0; scr.clipy1 = 150;
    for (i = 0; i < 5; i++) scr.planes[i] = pl[i];
    for (f = 1; f <= to; f++) {
        if (f >= from && (f - from) % every == 0) {
            char fn[64]; FILE *fo;
            memset(pl, 0, sizeof(pl)); memset(scratch, 0, sizeof(scratch));
            v3_draw(&sc, &scr);
            k_wait_blitter();
            sprintf(fn, "f%04d.raw", f);
            fo = fopen(fn, "wb"); fwrite(pl, 1, sizeof(pl), fo); fclose(fo);
        }
        v3_step(&sc);
    }
    return 0;
}
