#include <string.h>
#include "copy.h"
#include "net.h"
#include "nic.h"
#include "prodos.h"
#include "slot.h"

extern uint8_t nic_hole[];

static struct nic *vec;

static uint8_t load_file(const char *dir, const char *name)
{
    char path[PD_PATH_MAX + 1];
    struct nic *nvec;
    uint16_t got = 0;
    uint8_t err;
    uint8_t ref;

    vec = 0;
    strcpy(path, dir);
    if (!pd_path_join(path, name)) {
        net_log("Path too long");
        return NET_NOCARD;
    }
    memset(nic_hole, 0, NIC_HOLE_SIZE);
    err = pd_open(path, 0, &ref);
    if (err) {
        net_log(name);
        net_log("not on disk");
        return NET_NOCARD;
    }
    err = pd_read(ref, nic_hole, NIC_HOLE_SIZE, &got);
    pd_close(ref);
    if (err && err != PD_ERR_EOF) {
        net_log("Driver read failed");
        return NET_NOCARD;
    }
    nvec = (struct nic *)nic_hole;
    if (got < 8 || nvec->magic[0] != 'N' || nvec->magic[1] != 'I' ||
        nvec->magic[2] != 'C' || nvec->magic[3] != '1' || nvec->load_size != got) {
        net_log(name);
        net_log("is not a NIC overlay");
        return NET_NOCARD;
    }
    vec = nvec;
    return NET_OK;
}

static uint8_t find_uther(uint8_t configured)
{
    uint8_t (*probe)(uint8_t);
    uint8_t prefer;
    uint8_t mf;
    uint8_t s;

    /* The W5100 probe writes the mode byte back. MegaFlash does not read
     * that register back, so the restore hangs the machine. */
    mf = slot_megaflash();
    if (mf) {
        net_log("MegaFlash");
        return mf;
    }
    probe = vec->probe;
    if (configured >= 1 && configured <= 7) {
        if (slot_is_disk2(configured)) {
            net_log("Slot is a Disk II");
            return 0;
        }
        return probe(configured) ? configured : 0;
    }
    prefer = slot_prefer_uther();
    if (!slot_is_disk2(prefer) && probe(prefer)) {
        return prefer;
    }
    for (s = 1; s <= 7; s++) {
        if (s == prefer || slot_is_disk2(s)) {
            continue;
        }
        if (probe(s)) {
            return s;
        }
    }
    return 0;
}

uint8_t nic_bringup(void)
{
    const char *dir = copy_home();
    uint8_t slot;
    uint8_t err;
    char ip[16];

    net.up = 0;
    vec = 0;
    slot = slot_find_cogs(net.slot);
    if (slot) {
        err = load_file(dir, "SMBGET.COG");
        if (err) {
            return err;
        }
        net_log("CoGS");
    } else {
        if (net.slot >= 1 && net.slot <= 7 && slot_is_disk2(net.slot)) {
            net_log("Slot is a Disk II");
            return NET_NOCARD;
        }
        err = load_file(dir, "SMBGET.UTH");
        if (err) {
            return err;
        }
        slot = find_uther(net.slot);
        if (!slot) {
            net_log("No network card");
            return NET_NOCARD;
        }
        if (!slot_megaflash()) {
            net_log("Uthernet II");
        }
    }
    net.slot = slot;
    err = vec->init(slot);
    if (err) {
        net.up = 0;
        return err;
    }
    ip_str(net.ip, ip);
    net_log(ip);
    return NET_OK;
}

uint8_t net_tcp_connect(const uint8_t *ip, uint16_t port)
{
    uint32_t a;

    if (!vec) {
        return NET_TIMEOUT;
    }
    memcpy(&a, ip, 4);
    return vec->connect(a, port);
}

int16_t net_tcp_send(const uint8_t *p, uint16_t n)
{
    if (!vec) {
        return -1;
    }
    return vec->send(p, n);
}

int16_t net_tcp_recv(uint8_t *p, uint16_t max)
{
    if (!vec) {
        return -1;
    }
    return vec->recv(p, max);
}

void net_tcp_close(void)
{
    if (vec) {
        vec->close();
    }
}
