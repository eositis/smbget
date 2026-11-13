#include <string.h>
#include "smb2.h"
#include "names.h"

#define PKT_MAX 776u
#define H       4u                 /* SMB2 header offset (after 4-byte length) */

const char *dir_hname;
uint32_t smb_status;
uint8_t  smb_why;                  /* TCP failure detail: 1 send, 2 closed, 3 timeout */
uint8_t  smb_stage;                /* session setup step that failed (1 or 2) */

extern uint8_t smbpkt[PKT_MAX];       /* see ip65_outbuf.s */
#define pkt smbpkt
static uint16_t rlen;              /* bytes of SMB2 message in pkt after H */
static uint16_t msgid;                    /* wraps after 65536 requests, far beyond a session */
static uint8_t  up;
static uint8_t  sid[8];
static uint32_t treeid;

static void put16(uint16_t off, uint16_t v) { pkt[off] = (uint8_t)v; pkt[off + 1] = (uint8_t)(v >> 8); }
static void put32(uint16_t off, uint32_t v) { put16(off, (uint16_t)v); put16(off + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(uint16_t off) { return (uint16_t)(pkt[off] | (pkt[off + 1] << 8)); }
static uint32_t get32(uint16_t off) { return get16(off) | ((uint32_t)get16(off + 2) << 16); }

static void hdr(uint16_t cmd)
{
    memset(pkt, 0, 4 + 64 + 40);            /* header + fixed body part */
    pkt[H] = 0xFE; pkt[H + 1] = 'S'; pkt[H + 2] = 'M'; pkt[H + 3] = 'B';
    put16(H + 4, 64);
    put16(H + 12, cmd);
    put16(H + 14, 1);                       /* credit request */
    put16(H + 24, msgid++);                 /* message id; an 8-bit counter wrapped after 256 requests */
    put32(H + 36, treeid);
    memcpy(pkt + H + 40, sid, 8);
}

static uint8_t send_msg(uint16_t bodylen)
{
    uint16_t total = (uint16_t)(64 + bodylen);
    pkt[0] = 0;
    pkt[1] = 0;
    pkt[2] = (uint8_t)(total >> 8);
    pkt[3] = (uint8_t)total;
    if (net_tcp_send(pkt, (uint16_t)(4 + total)) < 0) { smb_why = 1; return SMB_E_TCP; }
    return SMB_OK;
}

/* Read one framed reply into pkt (excess beyond PKT_MAX is discarded). */
static uint8_t recv_msg(void)
{
    uint16_t t0 = net_ms(), have = 0, want = 4;
    uint32_t len = 0;
    uint8_t tmp[64];
    int16_t n;

    for (;;) {
        if (have < want && have < PKT_MAX) {
            n = net_tcp_recv(pkt + have, (uint16_t)((want < PKT_MAX ? want : PKT_MAX) - have));
        } else if (have < want) {
            n = net_tcp_recv(tmp, (uint16_t)(want - have > 64 ? 64 : want - have));
            if (n > 0) have = (uint16_t)(have + n), n = 0;
        } else {
            break;
        }
        if (n < 0) { smb_why = 2; return SMB_E_TCP; }
        if (n == 0) {
            if ((uint16_t)(net_ms() - t0) > 8000u) { smb_why = 3; return SMB_E_TCP; }
            continue;
        }
        have = (uint16_t)(have + n);
        t0 = net_ms();
        if (want == 4 && have >= 4) {
            len = ((uint32_t)pkt[1] << 16) | ((uint32_t)pkt[2] << 8) | pkt[3];
            if (len < 64 || len > 60000u) return SMB_E_PROTO;
            want = (uint16_t)(4 + len);
        }
    }
    rlen = (uint16_t)(have > PKT_MAX ? PKT_MAX : have) - 4;
    if (pkt[H] != 0xFE || pkt[H + 1] != 'S') return SMB_E_PROTO;
    smb_status = get32(H + 8);
    return SMB_OK;
}

static uint8_t is_ntlm_ok(void)
{
    return (uint8_t)(smb_status == 0);
}

/* ---- negotiate ----------------------------------------------------------- */

static uint8_t negotiate(void)
{
    uint8_t r, i;
    uint16_t b = H + 64;

    msgid = 0; treeid = 0;
    memset(sid, 0, 8);
    hdr(0x0000);
    put16(b, 36);                           /* StructureSize */
    put16(b + 2, 1);                        /* DialectCount */
    put16(b + 4, 1);                        /* SecurityMode: signing enabled */
    for (i = 0; i < 16; ++i) pkt[b + 12 + i] = (uint8_t)(0xA2 + i);   /* ClientGuid */
    put16(b + 36, 0x0202);                  /* dialect 2.0.2 */
    r = send_msg(38);
    if (r) return r;
    r = recv_msg();
    if (r) return r;
    if (smb_status) return SMB_E_NEG;
    if (get16(b) != 65) return SMB_E_NEG;
    return SMB_OK;
}

/* ---- session setup (raw NTLMSSP, anonymous) ------------------------------ */

static const uint8_t ntlm_sig[8] = { 'N','T','L','M','S','S','P',0 };

static uint8_t session_msg(uint16_t seclen)
{
    uint16_t b = H + 64;
    put16(b, 25);                           /* StructureSize */
    pkt[b + 3] = 1;                         /* SecurityMode */
    put16(b + 12, 88);                      /* SecurityBufferOffset */
    put16(b + 14, seclen);
    return send_msg((uint16_t)(24 + seclen));
}

/* SPNEGO wrappers (all lengths < 128, short-form DER) */
static const uint8_t spnego_init_pre[] = {
    0x60, 0x40, 0x06, 0x06, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x02,
    0xA0, 0x36, 0x30, 0x34,
    0xA0, 0x0E, 0x30, 0x0C, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0A,
    0xA2, 0x22, 0x04, 0x20
};

static uint8_t session_setup(void)
{
    uint8_t r;
    uint16_t s = H + 88, n;

    smb_stage = 1;
    /* NEGOTIATE (type 1) in NegTokenInit */
    hdr(0x0001);
    memcpy(pkt + s, spnego_init_pre, sizeof spnego_init_pre);
    n = (uint16_t)(s + sizeof spnego_init_pre);
    memset(pkt + n, 0, 32);
    memcpy(pkt + n, ntlm_sig, 8);
    put32(n + 8, 1);
    put32(n + 12, 0xE0088215UL);
    r = session_msg((uint16_t)(sizeof spnego_init_pre + 32));
    if (r) return r;
    r = recv_msg();
    if (r) return r;
    if (smb_status != 0xC0000016UL) return SMB_E_SESSION;   /* MORE_PROCESSING_REQUIRED */
    memcpy(sid, pkt + H + 40, 8);

    smb_stage = 2;
    /* AUTHENTICATE (type 3), empty credentials = anonymous, in NegTokenResp */
    hdr(0x0001);
    pkt[s] = 0xA1; pkt[s + 1] = 0x47;
    pkt[s + 2] = 0x30; pkt[s + 3] = 0x45;
    pkt[s + 4] = 0xA2; pkt[s + 5] = 0x43;
    pkt[s + 6] = 0x04; pkt[s + 7] = 0x41;
    n = (uint16_t)(s + 8);
    memset(pkt + n, 0, 65);
    memcpy(pkt + n, ntlm_sig, 8);
    put32(n + 8, 3);
    put16(n + 12, 1); put16(n + 14, 1); put32(n + 16, 64);  /* LM response: 1 zero byte */
    put32(n + 60, 0x00008A01UL);                    /* UNICODE|NTLM|ANONYMOUS|ALWAYS_SIGN, no key exchange */
    r = session_msg(73);
    if (r) return r;
    r = recv_msg();
    if (r) return r;
    return is_ntlm_ok() ? SMB_OK : SMB_E_SESSION;
}

/* ---- tree connect -------------------------------------------------------- */

static uint16_t put_utf16(uint16_t off, const char *s)
{
    while (*s) { pkt[off++] = (uint8_t)*s++; pkt[off++] = 0; }
    return off;
}

static uint8_t tree_connect(const uint8_t *ip, const char *share)
{
    uint8_t r;
    uint16_t b = H + 64, p = b + 8, end;
    char ips[16];

    hdr(0x0003);
    put16(b, 9);
    put16(b + 4, 72);                       /* PathOffset */
    ip_str(ip, ips);
    pkt[p] = '\\'; pkt[p + 1] = 0; pkt[p + 2] = '\\'; pkt[p + 3] = 0;
    end = put_utf16(p + 4, ips);
    pkt[end++] = '\\'; pkt[end++] = 0;
    end = put_utf16(end, share);
    put16(b + 6, (uint16_t)(end - p));
    r = send_msg((uint16_t)(end - b));
    if (r) return r;
    r = recv_msg();
    if (r) return r;
    if (smb_status) return SMB_E_TREE;
    treeid = get32(H + 36);
    up = 1;
    return SMB_OK;
}


/* ---- directory listing --------------------------------------------------- */

#define STATUS_NO_MORE_FILES 0x80000006UL

static uint8_t fileid[16];

/* "/a/b" -> UTF-16LE "a\b" at pkt[off]; returns the end offset. */
static uint16_t put_path(uint16_t off, const char *path)
{
    char c;

    if (*path == '/') ++path;
    for (; *path; ++path) {
        c = *path;
        if (c == '/') c = '\\';
        pkt[off++] = (uint8_t)c;
        pkt[off++] = 0;
    }
    return off;
}

static uint8_t obj_open(const char *path, uint32_t access, uint32_t options, uint8_t disp, uint32_t *eof)
{
    uint8_t r;
    uint16_t b = H + 64, end;

    hdr(0x0005);
    memset(pkt + b, 0, 57);
    put16(b, 57);
    put32(b + 4, 2);                        /* impersonation */
    put32(b + 24, access);
    put32(b + 32, 7);                       /* share read/write/delete */
    put32(b + 36, disp);                    /* 1 open, 2 create, 5 overwrite-if */
    put32(b + 40, options);
    put16(b + 44, 120);                     /* name offset */
    end = put_path(b + 56, path);
    put16(b + 46, (uint16_t)(end - (b + 56)));
    r = send_msg((uint16_t)(end == b + 56 ? 57 : end - b));
    if (r) return r;
    r = recv_msg();
    if (r) return r;
    if (smb_status) return SMB_E_STATUS;
    memcpy(fileid, pkt + b + 64, 16);
    if (eof) *eof = get32(b + 48);
    return SMB_OK;
}

static void dir_close(void)
{
    uint16_t b = H + 64;
    hdr(0x0006);
    put16(b, 24);
    memcpy(pkt + b + 8, fileid, 16);
    if (!send_msg(24)) recv_msg();
}

static uint8_t is_leap(uint16_t y)
{
    return (uint8_t)((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
}

/* FILETIME (100ns since 1601) -> ProDOS date word; hi = high 32 bits */
static uint16_t ft_to_date(uint32_t hi)
{
    static const uint8_t mdays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    uint32_t days = (hi * 100UL) / 20115UL;     /* hi units are 429.4967 s */
    uint16_t y = 1601, yd;
    uint8_t m, md;

    if (days >= 146097UL) { days -= 146097UL; y = 2001; }
    for (;;) {
        yd = is_leap(y) ? 366 : 365;
        if (days < yd) break;
        days -= yd;
        ++y;
    }
    for (m = 0; m < 12; ++m) {
        md = (uint8_t)(mdays[m] + (m == 1 && is_leap(y)));
        if (days < md) break;
        days -= md;
    }
    if (y < 1940 || y > 2039) return 0;
    return (uint16_t)(((y % 100) << 9) | ((m + 1) << 5) | (uint8_t)(days + 1));
}

uint8_t smb_readdir(const char *path, pd_dir_cb cb)
{
    uint8_t r, first = 1;
    uint16_t b = H + 64, off, next, nlen, i, o;
    uint32_t attrs, eof;
    fentry_t f;
    char host[80];

    if (!up) return SMB_E_TCP;
    r = obj_open(path, 0x00100081UL, 0x21, 1, 0);   /* list dir; directory file | sync io */
    if (r) return r;
    for (;;) {
        hdr(0x000E);
        memset(pkt + b, 0, 34);
        put16(b, 33);
        pkt[b + 2] = 0x01;                  /* FileDirectoryInformation */
        pkt[b + 3] = first ? 0x01 : 0x00;   /* restart scan on first call */
        memcpy(pkt + b + 8, fileid, 16);
        put16(b + 24, 96);
        put16(b + 26, 2);
        put32(b + 28, 700);               /* output buffer: 4+64+8+700 fits the 776-byte packet */
        pkt[b + 32] = '*';
        first = 0;
        r = send_msg(34);
        if (!r) r = recv_msg();
        if (r) break;
        if (smb_status == STATUS_NO_MORE_FILES) { r = SMB_OK; break; }
        if (smb_status) { r = SMB_E_STATUS; break; }
        off = (uint16_t)(H + get16(b + 2));
        o = off;
        for (;;) {
            if ((uint16_t)(o + 64) > H + rlen) break;
            next = get16(o);
            nlen = get16(o + 60);
            if (nlen > 158 || (uint16_t)(o + 64 + nlen) > H + rlen) break;
            for (i = 0; i < nlen / 2 && i < sizeof host - 1; ++i) {
                uint8_t c = pkt[o + 64 + i * 2];
                host[i] = (char)c;
                if (pkt[o + 65 + i * 2]) host[i] = '?';
            }
            host[i] = 0;
            if (strcmp(host, ".") && strcmp(host, "..")) {
                memset(&f, 0, sizeof f);
                attrs = get32(o + 56);
                eof = get32(o + 40);
                f.date = ft_to_date(get32(o + 28));
                if (attrs & 0x10) {
                    f.flags = F_DIR;
                    f.type = PT_DIR;
                    strncpy(f.name, host, PD_NAME_MAX);
                } else {
                    names_host_to_prodos(host, f.name, &f.type, &f.aux);
                    f.size = eof;
                    f.blocks = (uint16_t)((eof + 511UL) / 512UL);
                }
                dir_hname = host;
                if (cb(&f)) { next = 0; r = SMB_OK; goto stop; }
            }
            if (!next) break;
            o = (uint16_t)(o + next);
        }
    }
stop:
    dir_close();
    return r;
}


uint8_t *smb_scratch(uint16_t *size)
{
    *size = PKT_MAX;
    return pkt;
}

/* ---- file read ----------------------------------------------------------- */

#define READ_MAX SMB_READ_MAX

uint8_t smb_open_file(const char *path, uint32_t *size)
{
    if (!up) return SMB_E_TCP;
    return obj_open(path, 0x00120089UL, 0x60, 1, size);   /* read; non-directory | sync io */
}

void smb_close_file(void)
{
    dir_close();
}

/* Read up to want (<= READ_MAX) bytes at off. Returns a pointer into the packet buffer
   (valid until the next call) and sets *got; *got == 0 at end of file; NULL on error. */
const uint8_t *smb_read(uint32_t off, uint16_t want, uint16_t *got)
{
    uint16_t b = H + 64, doff, dlen;

    *got = 0;
    if (want > READ_MAX) want = READ_MAX;
    hdr(0x0008);
    memset(pkt + b, 0, 49);
    put16(b, 49);
    pkt[b + 2] = 0x50;
    put32(b + 4, want);
    put32(b + 8, off);
    memcpy(pkt + b + 16, fileid, 16);
    if (send_msg(49) || recv_msg()) return 0;
    if (smb_status == 0xC0000011UL) return pkt;          /* END_OF_FILE */
    if (smb_status) return 0;
    doff = (uint16_t)(H + pkt[b + 2]);
    dlen = (uint16_t)get32(b + 4);
    if (doff + dlen > H + rlen) return 0;
    *got = dlen;
    return pkt + doff;
}


/* ---- file create / write ------------------------------------------------- */

#define WRITE_MAX SMB_WRITE_MAX
#define WRITE_DATA (H + 64 + 48)       /* offset of the data area inside pkt */

/* Create (overwrite=0: fail if it exists -> SMB_E_STATUS with smb_status C0000035). */
uint8_t smb_create_file(const char *path, uint8_t overwrite)
{
    if (!up) return SMB_E_TCP;
    return obj_open(path, 0x00120116UL, 0x60, overwrite ? 5 : 2, 0);
}

uint8_t *smb_write_buf(void)
{
    return pkt + WRITE_DATA;
}

/* Send len (<= WRITE_MAX) bytes previously stored at smb_write_buf(), at file offset off. */
uint8_t smb_write(uint32_t off, uint16_t len)
{
    uint16_t b = H + 64;

    hdr(0x0009);                            /* clears only the header + 40 bytes; data sits beyond */
    memset(pkt + b, 0, 48);
    put16(b, 49);
    put16(b + 2, 112);                      /* data offset from header start */
    put32(b + 4, len);
    put32(b + 8, off);
    memcpy(pkt + b + 16, fileid, 16);
    if (send_msg((uint16_t)(48 + len)) || recv_msg()) return SMB_E_TCP;
    return smb_status ? SMB_E_STATUS : SMB_OK;
}


/* Set the open file's attributes to NORMAL (clears the archive bit, which Samba maps to the
   owner-execute permission on some shares: new files would otherwise look like executables). */
void smb_set_normal(void)
{
    uint16_t b = H + 64;

    hdr(0x0011);
    memset(pkt + b, 0, 72);
    put16(b, 33);
    pkt[b + 2] = 1;                         /* file information */
    pkt[b + 3] = 4;                         /* FileBasicInformation */
    put32(b + 4, 40);
    put16(b + 8, 96);                       /* buffer offset from header start */
    memcpy(pkt + b + 16, fileid, 16);
    put32(b + 32 + 32, 0x80);               /* FileAttributes = NORMAL; the four times stay 0 = unchanged */
    if (!send_msg(72)) recv_msg();
}

/* Create a directory. An existing one comes back as SMB_E_STATUS / 0xC0000035. */
uint8_t smb_mkdir(const char *path)
{
    uint8_t r;

    if (!up) return SMB_E_TCP;
    r = obj_open(path, 0x00100081UL, 0x00000001UL, 2, 0);   /* create, directory */
    if (r) return r;
    dir_close();
    return SMB_OK;
}

/* Delete a file or an empty directory (FILE_DELETE_ON_CLOSE, then close). */
uint8_t smb_remove(const char *path, uint8_t directory)
{
    uint8_t r;
    uint32_t opt = 0x00001000UL;

    if (!up) return SMB_E_TCP;
    if (directory) opt |= 0x00000001UL;
    else opt |= 0x00000040UL;
    r = obj_open(path, 0x00110080UL, opt, 1, 0);            /* delete access, open */
    if (r) return r;
    dir_close();
    return SMB_OK;
}

/* Rename. newpath is a share path; the server wants it from the share root. */
uint8_t smb_rename(const char *path, const char *newpath)
{
    uint8_t r;
    uint16_t b, name, end;

    if (!up) return SMB_E_TCP;
    r = obj_open(path, 0x00110080UL, 0, 1, 0);
    if (r) return r;
    b = H + 64;
    hdr(0x0011);
    memset(pkt + b, 0, 52);                 /* SET_INFO header + rename header */
    put16(b, 33);
    pkt[b + 2] = 1;                         /* file information */
    pkt[b + 3] = 10;                        /* FileRenameInformation */
    put32(b + 4, 20);                       /* replaced once the name length is known */
    put16(b + 8, 96);                       /* buffer offset from the SMB2 header */
    memcpy(pkt + b + 16, fileid, 16);
    name = (uint16_t)(b + 52);              /* UTF-16 name, after the 20-byte rename header */
    pkt[name] = '\\';
    pkt[name + 1] = 0;
    end = put_path((uint16_t)(name + 2), newpath);
    if (end > PKT_MAX) {
        dir_close();
        return SMB_E_PROTO;
    }
    put32((uint16_t)(b + 48), (uint32_t)(end - name));          /* FileNameLength */
    put32(b + 4, (uint32_t)(end - (b + 32)));                   /* BufferLength */
    r = send_msg((uint16_t)(end - b));
    if (!r) r = recv_msg();
    if (!r && smb_status) r = SMB_E_STATUS;
    dir_close();
    return r;
}

/* ---- public -------------------------------------------------------------- */

uint8_t smb_connect(const uint8_t *ip, const char *share, net_log_fn log)
{
    uint8_t r;

    log("TCP connect port 445...");
    if (net_tcp_connect(ip, 445)) return SMB_E_TCP;
    log("SMB2 negotiate...");
    r = negotiate();
    if (r) goto fail;
    log("Guest session setup...");
    r = session_setup();
    if (r) goto fail;
    log("Tree connect...");
    r = tree_connect(ip, share);
    if (r) goto fail;
    return SMB_OK;
fail:
    net_tcp_close();
    return r;
}

void smb_disconnect(void)
{
    up = 0;
    net_tcp_close();
}

uint8_t smb_connected(void)
{
    return up;
}

const char *smb_errstr(uint8_t e)
{
    switch (e) {
    case SMB_E_TCP:     return "TCP connection failed";
    case SMB_E_NEG:     return "SMB2 negotiate failed";
    case SMB_E_SESSION: return "Guest login refused";
    case SMB_E_TREE:    return "Share not accessible";
    case SMB_E_PROTO:   return "Bad reply from server";
    case SMB_E_STATUS:  return "Server error";
    }
    return "SMB error";
}
