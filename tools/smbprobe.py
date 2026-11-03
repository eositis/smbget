#!/usr/bin/env python3
"""Replay SMBGET's SMB2 negotiate + guest session setup against a server, with variants."""
import socket, struct, sys

HOST = sys.argv[1]
SIG = b"NTLMSSP\0"
INIT_PRE = bytes([0x60,0x40,0x06,0x06,0x2B,0x06,0x01,0x05,0x05,0x02,0xA0,0x36,0x30,0x34,
 0xA0,0x0E,0x30,0x0C,0x06,0x0A,0x2B,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x02,0x0A,0xA2,0x22,0x04,0x20])

def hdr(cmd, mid, sid=b"\0"*8, credit=1):
    return (b"\xfeSMB" + struct.pack("<HHIHHIIQIIQ"[:0] or "<H", 64) + struct.pack("<H", 0) + struct.pack("<I", 0)
            + struct.pack("<HH", cmd, credit) + struct.pack("<II", 0, 0) + struct.pack("<Q", mid)
            + struct.pack("<I", 0) + struct.pack("<I", 0) + sid + b"\0"*16)

def frame(b): return b"\0" + struct.pack(">I", len(b))[1:] + b

def recv(s):
    d = b""
    while len(d) < 4: d += s.recv(4 - len(d))
    n = int.from_bytes(d[1:4], "big"); m = b""
    while len(m) < n: m += s.recv(n - len(m))
    return m

def negotiate(s, dialects=(0x0202,), secmode=1):
    body = struct.pack("<HHHHI", 36, len(dialects), secmode, 0, 0) + bytes(0xA2+i for i in range(16)) + b"\0"*8
    body += b"".join(struct.pack("<H", d) for d in dialects)
    s.sendall(frame(hdr(0, 0) + body)); r = recv(s)
    return struct.unpack_from("<I", r, 8)[0], r

def type1(flags, version=False):
    m = SIG + struct.pack("<II", 1, flags) + b"\0"*16
    if version: m += bytes([6,1,0xB1,0x1D,0,0,0,15])
    return m

def spnego_init(ntlm):
    mech = bytes([0xA0,0x0E,0x30,0x0C,0x06,0x0A,0x2B,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x02,0x0A])
    tok = bytes([0xA2, len(ntlm)+2, 0x04, len(ntlm)]) + ntlm
    seq = bytes([0x30, len(mech)+len(tok)]) + mech + tok
    a0 = bytes([0xA0, len(seq)]) + seq
    oid = bytes([0x06,0x06,0x2B,0x06,0x01,0x05,0x05,0x02])
    return bytes([0x60, len(oid)+len(a0)]) + oid + a0

def session(s, sec, secmode=1, mid=1, credit=1):
    body = struct.pack("<HBBIIHHQ", 25, 0, secmode, 0, 0, 88, len(sec), 0) + sec
    s.sendall(frame(hdr(1, mid, credit=credit) + body)); r = recv(s)
    return struct.unpack_from("<I", r, 8)[0], r

def run(name, flags, version=False, raw=False, secmode=1, dialects=(0x0202,)):
    s = socket.create_connection((HOST, 445), timeout=5)
    st, r = negotiate(s, dialects)
    t1 = type1(flags, version)
    st2, r2 = session(s, t1 if raw else spnego_init(t1), secmode)
    print(f"{name:40s} neg={st:08X} dialect={struct.unpack_from('<H', r, 68)[0]:04X} session1={st2:08X}")
    s.close()

for name, kw in [
    ("as built b011 (spnego, no version)", dict(flags=0xE0088215)),
    ("spnego, flags w/ version bit, no blob", dict(flags=0xE2088215)),
    ("spnego, version flag + blob", dict(flags=0xE2088215, version=True)),
    ("raw ntlm, no version", dict(flags=0xE0088215, raw=True)),
    ("spnego, secmode 0", dict(flags=0xE0088215, secmode=0)),
    ("spnego, dialects 2.0.2+2.1", dict(flags=0xE0088215, dialects=(0x0202,0x0210))),
]:
    try: run(name, **kw)
    except Exception as e: print(name, "ERR", e)

# ---- step 2 variants ----
def spnego_resp(ntlm):
    t = bytes([0x04, len(ntlm)]) + ntlm
    a2 = bytes([0xA2, len(t)]) + t
    seq = bytes([0x30, len(a2)]) + a2
    return bytes([0xA1, len(seq)]) + seq

def type3(flags, lm=b"\0", nt=b"", user=b"", dom=b"", ws=b"", version=False, mic=False):
    pay_off = 64 + (8 if version else 0) + (16 if mic else 0)
    fields = b""; payload = b""
    for v in (lm, nt, dom, user, ws, b""):
        fields += struct.pack("<HHI", len(v), len(v), pay_off + len(payload)); payload += v
    m = SIG + struct.pack("<I", 3) + fields + struct.pack("<I", flags)
    if version: m += bytes([6,1,0xB1,0x1D,0,0,0,15])
    if mic: m += b"\0"*16
    return m + payload

def run2(name, t3, raw=False):
    s = socket.create_connection((HOST, 445), timeout=5)
    negotiate(s)
    st1, r = session(s, spnego_init(type1(0xE0088215)))
    sid = r[40:48]
    sec = t3 if raw else spnego_resp(t3)
    body = struct.pack("<HBBIIHHQ", 25, 0, 1, 0, 0, 88, len(sec), 0) + sec
    s.sendall(frame(hdr(1, 2, sid) + body)); r2 = recv(s)
    print(f"{name:44s} step2={struct.unpack_from('<I', r2, 8)[0]:08X}")
    s.close()

print("--- step 2 ---")
run2("anon flags E0088A15 (as built)", type3(0xE0088A15))
run2("anon, no ANONYMOUS flag", type3(0xE0088215))
run2("anon, nt resp empty, lm empty", type3(0xE0088A15, lm=b""))
run2("anon, raw (no spnego)", type3(0xE0088A15), raw=True)
run2("anon, + version blob, version flag", type3(0xE2088A15, version=True))
run2("anon, + version + mic space", type3(0xE2088A15, version=True, mic=True))
run2("anon, flags 0x60088A15", type3(0x60088A15))
run2("anon, flags 0xE0088A15|unicode-only 0x1|0x800|0x200|0x8000", type3(0x00008A01|0x200))
