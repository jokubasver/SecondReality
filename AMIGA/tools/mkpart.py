#!/usr/bin/env python3
"""Convert a part ELF (linked at 0 with -q) into the kernel's relocatable
part format: 'SRPT', image size, bss size, entry, nrel, rel[], image."""
import sys, struct
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

R_68K_32, R_68K_PC32, R_68K_PC16, R_68K_PC8 = 1, 4, 5, 6

def main(elfpath, out):
    f = ELFFile(open(elfpath, 'rb'))
    image = bytearray()
    sects = {}
    for name in ('.text', '.rodata', '.data'):
        s = f.get_section_by_name(name)
        if s is None or s['sh_size'] == 0: continue
        addr = s['sh_addr']
        if len(image) < addr: image += bytes(addr - len(image))
        assert len(image) == addr, (name, hex(addr), hex(len(image)))
        image += s.data()
        sects[name] = s
    bss = f.get_section_by_name('.bss')
    if bss is not None and bss['sh_size']:
        if len(image) < bss['sh_addr']: image += bytes(bss['sh_addr'] - len(image))
        bsssize = bss['sh_size']
    else:
        bsssize = 0
    symtab = f.get_section_by_name('.symtab')
    entry = None
    for sym in symtab.iter_symbols():
        if sym.name == 'part_main': entry = sym['st_value']
    assert entry is not None, 'no part_main'
    rels = []
    for s in f.iter_sections():
        if not isinstance(s, RelocationSection): continue
        tgt = f.get_section(s['sh_info']).name
        if tgt not in ('.text', '.rodata', '.data'): continue
        for r in s.iter_relocations():
            t = r['r_info_type']
            sym = symtab.get_symbol(r['r_info_sym'])
            if t in (R_68K_PC32, R_68K_PC16, R_68K_PC8):
                if sym['st_shndx'] == 'SHN_ABS':
                    sys.exit('PC-relative reference to kernel symbol %s' % sym.name)
                continue
            if t != R_68K_32:
                sys.exit('unsupported relocation type %d (%s)' % (t, sym.name))
            if sym['st_shndx'] == 'SHN_ABS':
                continue    # kernel symbol: absolute already
            rels.append(r['r_offset'])
    rels.sort()
    hdr = struct.pack('>4sIIII', b'SRPT', len(image), bsssize, entry, len(rels))
    hdr += b''.join(struct.pack('>I', x) for x in rels)
    open(out, 'wb').write(hdr + image)
    print('%s: image %d bss %d relocs %d' % (out, len(image), bsssize, len(rels)))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
