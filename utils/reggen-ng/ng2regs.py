#!/usr/bin/env python3
"""Translate a reggen-ng .reggen description into reggen .regs syntax.

A one-shot migration aid: reggen-ng and its checked-in headergen output are
being replaced by build-time generation with tools/reggen.  The point of
translating rather than re-transcribing is that .reggen is the source the
shipping headers were generated from, so the field layout cannot drift.
"""
import re
import sys

NUM = r"(?:[0-9]+|0[xX][0-9a-fA-F]+);?"


def tokenize(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r"#[^\n]*", " ", text)
    for m in re.finditer(r'"[^"]*"|[{}]|[^\s{}]+', text):
        yield m.group(0)


class P:
    def __init__(self, toks):
        self.t = list(toks)
        self.i = 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        v = self.peek()
        self.i += 1
        return v

    def word(self):
        return (self.next() or "").rstrip(";")

    def expect(self, v):
        g = self.next()
        if g != v:
            raise SystemExit("expected %r got %r near: %s"
                             % (v, g, " ".join(self.t[max(0, self.i - 12):self.i + 6])))

    def nums(self):
        out = []
        while re.fullmatch(NUM, self.peek() or ""):
            out.append(int(self.word(), 0))
        return out


def parse_enums(p):
    enums = []
    p.expect("{")
    while p.peek() != "}":
        k = p.word()
        if k == "enum":
            name = p.word()
            val = int(p.word(), 0)
            enums.append((val, name))
        elif k == "desc":
            p.next()
        else:
            raise SystemExit("unknown enum-body keyword %r" % k)
    p.expect("}")
    return enums


def parse_reg_body(p):
    """returns (fields, variants, instance, addr)"""
    fields, variants, inst, addr = [], [], None, None
    p.expect("{")
    while p.peek() != "}":
        k = p.word()
        if k == "desc":
            p.next()
        elif k == "addr":
            # a register may give its offset in its body instead of after
            # its name, eg. reg Z_GID2LD { desc "..." addr 0x7f0 }
            addr = int(p.word(), 0)
        elif k == "instance":
            v = p.nums()
            inst = (v[0], v[1], v[2]) if len(v) == 3 else (v[0], 0, 1)
        elif k == "variant":
            kind = p.word()
            variants.append((kind, int(p.word(), 0)))
        elif k in ("bit", "fld", "field"):
            # reggen-ng maps all three to one handler, so "bit" may carry two
            # bit numbers: "bit 13 8 TFL" really is bits 13:8, and the shipping
            # header agrees (BM_AIC_SR_TFL == 0x3f00).
            n = p.nums()
            if len(n) == 1:
                msb = lsb = n[0]
            elif len(n) == 2:
                msb, lsb = n
            else:
                raise SystemExit("field with %d bit numbers" % len(n))
            name = p.word()
            enums = parse_enums(p) if p.peek() == "{" else []
            fields.append((msb, lsb, name, enums))
        else:
            raise SystemExit("unknown reg-body keyword %r near: %s"
                             % (k, " ".join(p.t[max(0, p.i - 16):p.i + 8])))
    p.expect("}")
    return fields, variants, inst, addr


def parse_node(p):
    name = p.word()
    node = {"name": name, "addr": None, "inst": None, "desc": None,
            "regs": [], "subs": []}
    p.expect("{")
    while p.peek() != "}":
        k = p.word()
        if k == "title":
            p.next()
        elif k == "desc":
            node["desc"] = p.next().strip('"')
        elif k == "addr":
            node["addr"] = int(p.word(), 0)
        elif k == "instance":
            v = p.nums()
            if len(v) == 3:
                node["inst"] = (v[0], v[1], v[2])
            else:
                node["addr"] = v[0]
        elif k == "node":
            node["subs"].append(parse_node(p))
        elif k == "reg":
            rname = p.word()
            roff = int(p.word(), 0) if re.fullmatch(NUM, p.peek() or "") else None
            if p.peek() == "{":
                fields, variants, inst, baddr = parse_reg_body(p)
            else:
                fields, variants, inst, baddr = [], [], None, None
            if inst:
                roff = inst[0]
            elif baddr is not None:
                roff = baddr
            node["regs"].append((rname, roff, fields, variants, inst))
        else:
            raise SystemExit("unknown node keyword %r in %s" % (k, name))
    p.expect("}")
    return node


def parse(text):
    p = P(tokenize(text))
    nodes = []
    while p.peek() is not None:
        k = p.word()
        if k in ("name", "title", "isa", "version", "author"):
            p.next()
        elif k == "node":
            nodes.append(parse_node(p))
        else:
            raise SystemExit("unexpected top-level %r" % k)
    return nodes


def emit_fields(fields, ind):
    out = []
    for msb, lsb, name, enums in sorted(fields, key=lambda f: -f[0]):
        pos = "%02d %02d" % (msb, lsb) if msb != lsb else "-- %02d" % msb
        if enums:
            body = "; ".join("%d = %s" % (v, e) for v, e in sorted(enums))
            out.append("%s%s %s : enum { %s }" % (ind, pos, name, body))
        else:
            out.append("%s%s %s" % (ind, pos, name))
    return out


def emit_regs(regs, ind):
    L = []
    for rname, roff, fields, variants, inst in regs:
        arr = "" if not inst or inst[2] == 1 else " [%d; 0x%x]" % (inst[2], inst[1])
        if variants:
            # reggen has no variant concept: the set/clear aliases become
            # ordinary registers, with the offsets already summed.
            #
            # The fields are repeated on each rather than shared through a
            # named type, because reggen names field constants after the TYPE:
            # a shared "ENABLE_REG" would yield BM_OST_ENABLE_REG_OST1 where
            # the headers being replaced say BM_OST_ENABLE_OST1.
            for iname, ioff in ([(rname, roff)] +
                                [("%s_%s" % (rname, k.upper()), roff + v)
                                 for k, v in variants]):
                if fields:
                    L.append("%s%s @ 0x%02x%s : reg {" % (ind, iname, ioff, arr))
                    L += emit_fields(fields, ind + "    ")
                    L.append("%s}" % ind)
                else:
                    L.append("%s%s @ 0x%02x%s : reg" % (ind, iname, ioff, arr))
        elif fields:
            L.append("%s%s @ 0x%02x%s : reg {" % (ind, rname, roff, arr))
            L += emit_fields(fields, ind + "    ")
            L.append("%s}" % ind)
        else:
            L.append("%s%s @ 0x%02x%s : reg" % (ind, rname, roff, arr))
        L.append("")
    return L


def emit_node(node, ind, top):
    L = []
    if node["desc"]:
        L.append("%s// %s" % (ind, node["desc"]))
    if node["inst"]:
        base, stride, count = node["inst"]
        head = "%s%s @ 0x%08x [%d; 0x%x] : block {" % (ind, node["name"], base, count, stride)
    else:
        fmt = "0x%08x" if top else "0x%02x"
        head = ("%s%s @ " + fmt + " : block {") % (ind, node["name"], node["addr"])
    L.append(head)
    L += emit_regs(node["regs"], ind + "    ")
    for sub in node["subs"]:
        L += emit_node(sub, ind + "    ", False)
        L.append("")
    while L and L[-1] == "":
        L.pop()
    L.append("%s}" % ind)
    return L


def emit(nodes):
    L = ["// Register definitions for the Ingenic X1000.",
         "//",
         "// Translated from utils/reggen-ng/x1000.reggen, the description the",
         "// checked-in headers were generated from.",
         ""]
    for n in nodes:
        L += emit_node(n, "", True)
        L.append("")
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    nodes = parse(open(sys.argv[1]).read())
    nregs = sum(len(n["regs"]) + sum(len(s["regs"]) for s in n["subs"]) for n in nodes)
    sys.stderr.write("parsed %d nodes, %d registers\n" % (len(nodes), nregs))
    open(sys.argv[2], "w").write(emit(nodes))
