"""Demo file manifest: the order here defines file ids (F_<name>) and disk layout."""

# (output name, module, start orders [a-b = stop at b], sample budget bytes)
MUSIC = [
    ('mus0', '../MAIN/MUSIC0.S3M', '0,18-28,25', 200000),
    ('mus1', '../MAIN/MUSIC1.S3M', '0', 240000),
]

# part directories under src/parts
PARTS = ['test', 'alku']

# (id, path, packing: lz | delta | store, disk)
FILES = [
    ('MUS0_BANK', 'build/music/mus0.smp', 'delta', 1),
    ('MUS0_S0', 'build/music/mus0_0.str', 'store', 1),
    ('MUS1_BANK', 'build/music/mus1.smp', 'delta', 1),
    ('MUS1_S0', 'build/music/mus1_0.str', 'store', 1),
    ('MUS0_S18', 'build/music/mus0_18.str', 'store', 1),
    ('MUS0_S25', 'build/music/mus0_25.str', 'store', 1),
    ('PART_TEST', 'build/parts/test.prt', 'lz', 1),
    ('PART_ALKU', 'build/parts/alku.prt', 'lz', 1),
]
