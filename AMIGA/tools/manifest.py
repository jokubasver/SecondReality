"""Demo file manifest: the order here defines file ids (F_<name>) and disk layout."""

# (output name, module, start orders [a-b = stop at b], sample budget bytes)
MUSIC = [
    ('mus0', '../MAIN/MUSIC0.S3M', '0,18-28,25', 200000),
    ('mus1', '../MAIN/MUSIC1.S3M', '0', 240000),
]

# part directories under src/parts
PARTS = ['test', 'alku', 'u2a', 'pam', 'beglogo', 'glenz', 'tunneli', 'techno', 'panic', 'mntscrl', 'lens', 'plz', 'dots', 'water', 'coman', 'jplogo']

# (id, path, packing: lz | delta | store, disk)
FILES = [
    ('MUS0_BANK', 'build/music/mus0.smp', 'delta', 1),
    ('MUS0_S0', 'build/music/mus0_0.str', 'store', 1),
    ('MUS1_BANK', 'build/music/mus1.smp', 'delta', 1),
    ('MUS1_S0', 'build/music/mus1_0.str', 'store', 1),
    ('PART_TEST', 'build/parts/test.prt', 'lz', 1),
    ('PART_ALKU', 'build/parts/alku.prt', 'lz', 1),
    ('PART_U2A', 'build/parts/u2a.prt', 'lz', 1),
    ('PART_PAM', 'build/parts/pam.prt', 'lz', 1),
    ('PART_BEGLOGO', 'build/parts/beglogo.prt', 'lz', 1),
    ('PART_GLENZ', 'build/parts/glenz.prt', 'lz', 1),
    ('PART_TUNNELI', 'build/parts/tunneli.prt', 'lz', 1),
    ('PART_TECHNO', 'build/parts/techno.prt', 'lz', 1),
    # ---- disk 2
    ('PART_PANIC', 'build/parts/panic.prt', 'lz', 2),
    ('PART_MNTSCRL', 'build/parts/mntscrl.prt', 'lz', 2),
    ('PART_LENS', 'build/parts/lens.prt', 'lz', 2),
    ('PART_PLZ', 'build/parts/plz.prt', 'lz', 2),
    ('PART_DOTS', 'build/parts/dots.prt', 'lz', 2),
    ('PART_WATER', 'build/parts/water.prt', 'lz', 2),
    ('PART_COMAN', 'build/parts/coman.prt', 'lz', 2),
    ('PART_JPLOGO', 'build/parts/jplogo.prt', 'lz', 2),
    ('MUS0_BANK2', 'build/music/mus0.smp', 'delta', 2),
    ('MUS0_S18', 'build/music/mus0_18.str', 'store', 2),
    ('MUS0_S25', 'build/music/mus0_25.str', 'store', 2),
]
