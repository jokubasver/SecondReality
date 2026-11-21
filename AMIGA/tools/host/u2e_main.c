#include "kernel.h"
#include "visu3d.c"
#include "u2e_scene.h"
static FILE *fo;
void host_fill(int n, const s16 *xs, const s16 *ys, int color)
{
    int i;
    fprintf(fo, "%d", color);
    for (i = 0; i < n; i++) fprintf(fo, " %d %d", xs[i], ys[i]);
    fprintf(fo, "\n");
}
int main(void)
{
    static struct v3scene sc;
    struct v3screen scr;
    static u8 pl[5][6000], scratch[6080];
    int f, i;
    fo = fopen("polys.txt", "w");
    sc.objs = u2e_objs; sc.inst = u2e_inst; sc.conum = U2E_CONUM; sc.streams = u2e_streams;
    sc.nstreams = U2E_NSTREAMS; sc.kind = U2E_KIND; sc.colmap = u2e_colmap; sc.iflags = u2e_iflags;
    v3_init(&sc); v3_step(&sc);
    scr.scratch = scratch; scr.h = 150; scr.addx = 159; scr.addy = 74; scr.clipy0 = 0; scr.clipy1 = 150;
    for (i = 0; i < 5; i++) scr.planes[i] = pl[i];
    for (f = 1; f <= 1200; f++) {
        if (f == 100 || f == 200 || f == 400 || f == 600) {
            fprintf(fo, "frame %d\n", f);
            v3_draw(&sc, &scr);
        }
        v3_step(&sc);
    }
    fclose(fo);
    return 0;
}
