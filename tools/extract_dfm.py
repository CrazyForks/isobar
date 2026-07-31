#!/usr/bin/env python3
"""Extract VCL form layouts (binary DFM / RCDATA resources) from a PE32
executable and dump them as human-readable text.

Pure stdlib. Usage:

    python3 tools/extract_dfm.py kgfax.exe extracted/dfm-full.txt

Binary DFM format (Delphi/BCB TReader/TWriter stream):
  'TPF0' signature, then a recursive object:
    [0xFF <flags> [childpos:int32]]   (optional prefix, rare)
    class name : short string (1-byte length)
    object name: short string
    properties : repeated (name: short string, value), ended by empty name
    children   : vaNull (0) or vaList (1) + child objects + vaNull (0)

Value type bytes (TValueType ordinals):
  0 vaNull, 1 vaList, 2 vaInt8, 3 vaInt16, 4 vaInt32, 5 vaExtended,
  6 vaString, 7 vaIdent, 8 vaFalse, 9 vaTrue, 10 vaBinary, 11 vaSet,
  12 vaLString, 13 vaNil, 14 vaCollection, 15 vaSingle, 16 vaCurrency,
  17 vaDate, 18 vaWString, 19 vaInt64, 20 vaUTF8String, 21 vaDouble
"""

import struct
import sys

# ---------------------------------------------------------------- PE parsing

class PEResources:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe_off = struct.unpack_from('<I', d, 0x3C)[0]
        if d[pe_off:pe_off + 4] != b'PE\0\0':
            raise ValueError('not a PE file')
        nsects = struct.unpack_from('<H', d, pe_off + 6)[0]
        optsize = struct.unpack_from('<H', d, pe_off + 20)[0]
        sec = pe_off + 24 + optsize
        self.rsrc = None
        for i in range(nsects):
            name = d[sec + i * 40: sec + i * 40 + 8].rstrip(b'\0')
            vsize, vaddr, rsize, roff = struct.unpack_from('<IIII', d, sec + i * 40 + 8)
            if name == b'.rsrc':
                self.rsrc = (vaddr, roff, rsize)
        if not self.rsrc:
            raise ValueError('no .rsrc section')

    def rva_to_off(self, rva):
        vaddr, roff, _ = self.rsrc
        return roff + (rva - vaddr)

    def entries(self):
        """Yield (path, raw_bytes) for every resource; path is
        [type, name, language] where type/name are ints or strings."""
        d = self.data
        _, roff, _ = self.rsrc
        out = []

        def walk(off, path):
            n_named, n_id = struct.unpack_from('<HH', d, off + 12)
            for i in range(n_named + n_id):
                name, sub = struct.unpack_from('<II', d, off + 16 + i * 8)
                if name & 0x80000000:
                    noff = roff + (name & 0x7FFFFFFF)
                    slen = struct.unpack_from('<H', d, noff)[0]
                    nm = d[noff + 2: noff + 2 + slen * 2].decode('utf-16-le')
                else:
                    nm = name & 0xFFFF
                if sub & 0x80000000:
                    walk(roff + (sub & 0x7FFFFFFF), path + [nm])
                else:
                    de = roff + sub
                    drva, dsize, _dcp = struct.unpack_from('<III', d, de)
                    o = self.rva_to_off(drva)
                    out.append((path + [nm], d[o: o + dsize]))

        walk(roff, [])
        return out


# ---------------------------------------------------------------- DFM parser

TYPE_NAMES = {
    0: 'vaNull', 1: 'vaList', 2: 'vaInt8', 3: 'vaInt16', 4: 'vaInt32',
    5: 'vaExtended', 6: 'vaString', 7: 'vaIdent', 8: 'vaFalse', 9: 'vaTrue',
    10: 'vaBinary', 11: 'vaSet', 12: 'vaLString', 13: 'vaNil',
    14: 'vaCollection', 15: 'vaSingle', 16: 'vaCurrency', 17: 'vaDate',
    18: 'vaWString', 19: 'vaInt64', 20: 'vaUTF8String', 21: 'vaDouble',
}

class ParseError(Exception):
    pass

class DfmObject:
    def __init__(self, class_name, name, inherited=False, inline=False,
                 child_pos=-1):
        self.class_name = class_name
        self.name = name
        self.inherited = inherited
        self.inline = inline
        self.child_pos = child_pos
        self.props = []      # list of (name, value); value may be str/int/...
        self.children = []   # list of DfmObject

    def get(self, key, default=None):
        for k, v in self.props:
            if k.lower() == key.lower():
                return v
        return default

class Reader:
    def __init__(self, data, source='<stream>'):
        self.d = data
        self.p = 0
        self.source = source
        self.warnings = []

    def eof(self):
        return self.p >= len(self.d)

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def read(self, fmt):
        size = struct.calcsize(fmt)
        vals = struct.unpack_from(fmt, self.d, self.p)
        self.p += size
        return vals[0] if len(vals) == 1 else vals

    def short_str(self):
        n = self.u8()
        raw = self.d[self.p: self.p + n]
        self.p += n
        return decode_ansi(raw)

    def value(self):
        """Read one typed value. Returns a python value; type tag included
        for opaque ones."""
        t = self.u8()
        if t == 0:                      # vaNull
            return None
        if t == 1:                      # vaList
            items = []
            while True:
                if self.d[self.p] == 0:
                    self.p += 1
                    break
                items.append(self.value())
            return ListValue(items)
        if t == 2:
            return self.read('<b')
        if t == 3:
            return self.read('<h')
        if t == 4:
            return self.read('<i')
        if t == 5:                      # vaExtended: 80-bit float
            raw = self.d[self.p: self.p + 10]
            self.p += 10
            return ext80_to_float(raw)
        if t == 6:                      # vaString (short string, ANSI)
            return self.short_str()
        if t == 7:                      # vaIdent
            return Ident(self.short_str())
        if t == 8:
            return False
        if t == 9:
            return True
        if t == 10:                     # vaBinary
            n = self.read('<i')
            raw = self.d[self.p: self.p + n]
            self.p += n
            return BinaryValue(raw)
        if t == 11:                     # vaSet: idents, ended by empty str
            items = []
            while True:
                s = self.short_str()
                if s == '':
                    break
                items.append(s)
            return SetValue(items)
        if t == 12:                     # vaLString: int32 len ANSI string
            n = self.read('<i')
            raw = self.d[self.p: self.p + n]
            self.p += n
            return decode_ansi(raw)
        if t == 13:                     # vaNil
            return None
        if t == 14:                     # vaCollection
            return self.collection()
        if t == 15:                     # vaSingle
            return self.read('<f')
        if t == 16:                     # vaCurrency
            return self.read('<q') / 10000.0
        if t == 17:                     # vaDate (double)
            return self.read('<d')
        if t == 18:                     # vaWString: int32 len, UTF-16LE
            n = self.read('<i')
            raw = self.d[self.p: self.p + n * 2]
            self.p += n * 2
            return raw.decode('utf-16-le', errors='replace')
        if t == 19:                     # vaInt64
            return self.read('<q')
        if t == 20:                     # vaUTF8String
            n = self.read('<i')
            raw = self.d[self.p: self.p + n]
            self.p += n
            return raw.decode('utf-8', errors='replace')
        if t == 21:                     # vaDouble
            return self.read('<d')
        raise ParseError('%s: unknown value type 0x%02x at offset 0x%x'
                         % (self.source, t, self.p - 1))

    def collection(self):
        items = []
        idx = 0
        while True:
            t = self.u8()
            if t == 0:                  # vaNull: end of collection
                break
            if t != 1:                  # each item begins with vaList
                raise ParseError('%s: bad collection item marker 0x%02x at 0x%x'
                                 % (self.source, t, self.p - 1))
            props = []
            while True:
                name = self.short_str()
                if name == '':
                    break
                props.append((name, self.value()))
            items.append(CollectionItem(idx, props))
            idx += 1
        return CollectionValue(items)

    def parse_object(self):
        inherited = inline = False
        child_pos = -1
        if self.d[self.p] == 0xFF:      # object flags prefix
            self.p += 1
            flags = self.u8()
            inherited = bool(flags & 1)   # ffInherited
            if flags & 2:                 # ffChildPos
                child_pos = self.read('<i')
            inline = bool(flags & 4)      # ffInline
        class_name = self.short_str()
        name = self.short_str()
        obj = DfmObject(class_name, name, inherited, inline, child_pos)
        # properties
        while True:
            prop = self.short_str()
            if prop == '':
                break
            obj.props.append((prop, self.value()))
        # children: direct sequence of objects, terminated by a 0 byte
        while self.d[self.p] != 0:
            obj.children.append(self.parse_object())
        self.p += 1
        return obj


def decode_ansi(raw):
    try:
        return raw.decode('cp932')
    except UnicodeDecodeError:
        return raw.decode('cp932', errors='replace')


def ext80_to_float(raw):
    """Convert an 80-bit x87 extended float to a python float."""
    sign_exp, mantissa = struct.unpack('<HQ', raw)
    sign = -1 if sign_exp & 0x8000 else 1
    exp = sign_exp & 0x7FFF
    if exp == 0:
        return 0.0
    return sign * mantissa * (2.0 ** (exp - 16383 - 63))


class ListValue(list):
    pass

class SetValue(list):
    pass

class BinaryValue(bytes):
    pass

class Ident(str):
    pass

class CollectionItem:
    def __init__(self, index, props):
        self.index = index
        self.props = props

class CollectionValue(list):
    pass


# ---------------------------------------------------------------- text dump

def fmt_value(v, indent):
    pad = '  ' * indent
    if isinstance(v, BinaryValue):
        return '<binary, %d bytes>' % len(v)
    if isinstance(v, CollectionValue):
        lines = ['<']
        for it in v:
            lines.append(pad + '  item')
            for k, pv in it.props:
                lines.append(pad + '    %s = %s' % (k, fmt_value(pv, indent + 2)))
        lines.append(pad + '  end')
        return '\n'.join(lines)
    if isinstance(v, SetValue):
        return '[' + ', '.join(v) + ']'
    if isinstance(v, ListValue):
        return '(' + ', '.join(repr(x) for x in v) + ')'
    if isinstance(v, Ident):
        return str(v)
    if isinstance(v, str):
        return "'%s'" % v.replace("'", "''")
    if isinstance(v, bool):
        return 'True' if v else 'False'
    if v is None:
        return 'nil'
    if isinstance(v, float):
        return repr(v)
    return str(v)


def dump_object(obj, out, indent):
    pad = '  ' * indent
    kw = 'object'
    if obj.inherited:
        kw = 'inherited'
    elif obj.inline:
        kw = 'inline'
    out.append('%s%s %s: %s' % (pad, kw, obj.name, obj.class_name))
    for k, v in obj.props:
        out.append('%s  %s = %s' % (pad, k, fmt_value(v, indent + 1)))
    for c in obj.children:
        dump_object(c, out, indent + 1)
    out.append('%send' % pad)


def count_controls(obj):
    return sum(1 + count_controls(c) for c in obj.children)


def walk_objects(obj, depth=0, parent=None):
    yield obj, depth, parent
    for c in obj.children:
        yield from walk_objects(c, depth + 1, obj)


# ---------------------------------------------------------------- main

def extract(exe_path):
    pe = PEResources(exe_path)
    forms = []
    for path, raw in pe.entries():
        if path[0] == 10 and raw[:4] == b'TPF0':   # RT_RCDATA, DFM magic
            r = Reader(raw, source=str(path[1]))
            r.p = 4                     # skip 'TPF0' signature
            obj = r.parse_object()
            if not r.eof():
                r.warnings.append('%s: %d trailing bytes after object'
                                  % (path[1], len(raw) - r.p))
            forms.append((path[1], obj, raw, r.warnings))
    forms.sort(key=lambda f: f[0])
    return forms


def gen_layout_doc(forms, out_path):
    """Distilled layout facts for the clean-room GUI replica.
    Contains only functional facts: names, classes, geometry, captions."""
    lines = []
    lines.append('# KG-FAX GUI layout reference')
    lines.append('')
    lines.append('Distilled from the VCL form resources (binary DFM) of the')
    lines.append('original program. Facts only: control names, classes,')
    lines.append('positions, sizes, and captions. All coordinates are VCL')
    lines.append('design-time pixels (PixelsPerInch = 96), relative to each')
    lines.append("control's parent. Nested controls are shown with their")
    lines.append('parent in parentheses.')
    lines.append('')
    lines.append('Extracted with `tools/extract_dfm.py` (full verbose dump,')
    lines.append('private working material, is in `extracted/dfm-full.txt`).')
    lines.append('')

    for res_name, obj, raw, warnings in forms:
        cw = obj.get('ClientWidth', '?')
        ch = obj.get('ClientHeight', '?')
        lines.append('## %s (%s, %s) -- client area %sx%s'
                     % (obj.name, obj.class_name, res_name, cw, ch))
        cap = obj.get('Caption')
        if cap:
            lines.append('')
            lines.append('Window caption: `%s`' % cap)
        lines.append('')
        lines.append('| Control | Class | Left | Top | Width | Height | Caption |')
        lines.append('|---|---|---|---|---|---|---|')
        for o, depth, parent in walk_objects(obj):
            if depth == 0:
                continue

            def cell(key):
                v = o.get(key)
                return str(v) if v is not None else '—'

            capv = o.get('Caption') or ''
            name = o.name
            if parent is not obj:
                name = '%s (%s)' % (o.name, parent.name)
            lines.append('| %s | %s | %s | %s | %s | %s | %s |'
                         % (name, o.class_name, cell('Left'), cell('Top'),
                            cell('Width'), cell('Height'), capv))
        lines.append('')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def main():
    exe_path = sys.argv[1] if len(sys.argv) > 1 else 'kgfax.exe'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'extracted/dfm-full.txt'
    doc_path = sys.argv[3] if len(sys.argv) > 3 else None

    forms = extract(exe_path)

    out = []
    for res_name, obj, raw, warnings in forms:
        out.append('=' * 72)
        out.append('resource %s  (%d bytes)' % (res_name, len(raw)))
        out.append('=' * 72)
        dump_object(obj, out, 0)
        for w in warnings:
            out.append('! WARNING: ' + w)
        out.append('')

    import os
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')

    for res_name, obj, raw, warnings in forms:
        n_children = count_controls(obj)
        print('%-8s %-12s %sx%s  controls: %d%s'
              % (res_name, obj.name,
                 obj.get('ClientWidth', '?'), obj.get('ClientHeight', '?'),
                 n_children,
                 '  [WARNINGS]' if warnings else ''))
        for w in warnings:
            print('    !', w)
    print('\nwrote %s' % out_path)
    if doc_path:
        gen_layout_doc(forms, doc_path)
        print('wrote %s' % doc_path)


if __name__ == '__main__':
    main()
