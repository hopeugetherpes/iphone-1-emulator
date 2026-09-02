#include "soc.h"
#include <stdio.h>
#include <string.h>

static int pass, fail;
#define CHECK(x, ...) do { if (x) pass++; else { fail++; \
    printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void txn(s5l_spi_slave_t *s, const uint8_t *tx, uint8_t *rx,
                unsigned n) {
    s5l_mtz1_select_pin(s->ctx, false);
    for (unsigned i = 0; i < n; i++) rx[i] = s->transfer(s->ctx, tx[i]);
    s5l_mtz1_select_pin(s->ctx, true);
}

static uint16_t sum(const uint8_t *p, unsigned n) {
    uint16_t v = 0;
    while (n--) v = (uint16_t)(v + *p++);
    return v;
}

int main(void) {
    s5l_mtz1_t d;
    s5l_spi_slave_t s;
    uint8_t tx[1024], rx[1024];
    memset(&d, 0, sizeof d);
    s5l_mtz1_reset(&d);
    s5l_mtz1_bind(&d, &s);
    s5l_mtz1_power_pin(&d, true);

    memset(tx, 0, sizeof tx);
    tx[0] = 0xc2u;
    tx[1] = 0x40u;
    uint16_t ck = sum(tx, 1022u);
    tx[1022] = (uint8_t)(ck >> 8);
    tx[1023] = (uint8_t)ck;
    txn(&s, tx, rx, sizeof tx);
    uint8_t check[4] = { 5u, 0u, 0u, 6u };
    txn(&s, check, rx, sizeof check);
    CHECK(rx[0] == 0xd0u && rx[1] == 0u &&
          rx[2] == (uint8_t)(ck >> 8) && rx[3] == (uint8_t)ck,
          "A-speed checksum");

    uint8_t c3[4] = { 0xc3u, 0x3eu, 0x3eu, 0x3eu };
    txn(&s, c3, rx, sizeof c3);
    uint8_t raw[3] = { 0x11u, 0x22u, 0x33u };
    txn(&s, raw, rx, sizeof raw);
    txn(&s, check, rx, sizeof check);
    uint8_t c4[4] = { 0xc4u, 0u, 0u, 0xc4u };
    txn(&s, c4, rx, sizeof c4);
    CHECK(d.firmware, "B-speed execute enters firmware");

    uint8_t d0[4] = { 0xd0u, 0u, 0u, 0u };
    txn(&s, d0, rx, sizeof d0);
    CHECK(rx[0] == 0xaau && rx[1] == 1u && rx[2] == 2u && rx[3] == 0u,
          "interface version");

    uint8_t ri[8] = { 0x8fu, 0xd3u, 0u, 0u, 0u, 0u, 0u, 0u };
    txn(&s, ri, rx, sizeof ri);
    uint16_t ric = (uint16_t)(0xd3u + 5u);
    CHECK(rx[0] == 0xaau && rx[4] == 0u && rx[5] == 5u &&
          rx[6] == (uint8_t)(ric >> 8) && rx[7] == (uint8_t)ric,
          "report info");
    uint8_t gr[11] = { 0x82u, 0xd3u };
    txn(&s, gr, rx, sizeof gr);
    CHECK(rx[4] == 1u && rx[5] == 15u && rx[6] == 10u &&
          rx[7] == 1u && rx[8] == 0u &&
          rx[9] == 0u && rx[10] == 0xd3u + 27u, "report body/checksum");

    uint8_t detect_info[8] = { 0x8fu, 0xbfu, 0u, 0u, 0u, 0u, 0u, 0u };
    txn(&s, detect_info, rx, sizeof detect_info);
    CHECK(rx[0] == 0xaau && rx[4] == 0u && rx[5] == 4u &&
          rx[6] == 0u && rx[7] == 0xc3u, "input detection report info");
    uint8_t detect_read[10] = { 0x82u, 0xbfu };
    txn(&s, detect_read, rx, sizeof detect_read);
    CHECK(rx[4] == 0u && rx[5] == 0u && rx[6] == 0u && rx[7] == 0u &&
          rx[8] == 0u && rx[9] == 0xbfu, "input detection report read");

    uint8_t detect_set[10] = { 0x92u, 0xbfu, 0u, 0u, 1u, 2u, 3u, 4u,
                               0u, 0u };
    detect_set[8] = 0u;
    detect_set[9] = 0xc9u;
    txn(&s, detect_set, rx, sizeof detect_set);
    CHECK(rx[0] == 0xaau && rx[8] == 0u && rx[9] == 0xc9u &&
          d.input_detect[0] == 1u && d.input_detect[1] == 2u &&
          d.input_detect[2] == 3u && d.input_detect[3] == 4u,
          "set input detection report");
    txn(&s, detect_read, rx, sizeof detect_read);
    CHECK(rx[4] == 1u && rx[5] == 2u && rx[6] == 3u && rx[7] == 4u &&
          rx[8] == 0u && rx[9] == 0xc9u,
          "read input detection report");

    uint8_t sr[7] = { 0x92u, 0xabu, 0u, 0u, 1u, 0u, 0u };
    uint16_t src = 0xacu;
    sr[5] = (uint8_t)(src >> 8);
    sr[6] = (uint8_t)src;
    txn(&s, sr, rx, sizeof sr);
    CHECK(d.operating_mode == 1u && rx[5] == sr[5] && rx[6] == sr[6],
          "set operating mode");

    uint8_t lr[8] = { 0x46u, 0x46u, 0x46u, 0x46u,
                      0x46u, 0x46u, 0x46u, 0x46u };
    txn(&s, lr, rx, sizeof lr);
    CHECK(rx[0] == 0xaau && rx[1] == 0u && rx[2] == 0u &&
          rx[3] == 0u && rx[4] == 0u, "idle length");

    s5l_mt_contact_t c = { 1u, MTZ2_PHASE_MAKE_TOUCH, 48u, 60u,
                           255u, 8u, 8u };
    CHECK(s5l_mtz1_set_contacts(&d, &c, 1u), "queue contact");
    txn(&s, lr, rx, sizeof lr);
    unsigned l = rx[1] << 8 | rx[2];
    uint16_t lc = (uint16_t)(rx[1] + rx[2]);
    CHECK(l == 44u && rx[3] == (uint8_t)(lc >> 8) &&
          rx[4] == (uint8_t)lc && d.pending_len != 0u && d.atn,
          "contact length/checksum/pending");
    uint8_t data[45] = { 0x47u };
    txn(&s, data, rx, sizeof data);
    uint16_t dc = 0u;
    for (unsigned i = 1; i < 43u; i++) dc = (uint16_t)(dc + rx[i]);
    CHECK(!d.atn && rx[1] == 0xccu && rx[4] == 1u &&
          rx[43] == (uint8_t)(dc >> 8) && rx[44] == (uint8_t)dc,
          "contact data/checksum/ATN");

    s5l_mtz2_t z2;
    uint8_t a[MTZ2_PAYLOAD_LIMIT], b[MTZ2_PAYLOAD_LIMIT];
    s5l_mtz2_reset(&z2);
    unsigned na = s5l_mtz2_encode(&z2, &c, 1u, 7u, 123u, a);
    s5l_mt_geom_t geom = { z2.family_id, z2.endianness, z2.rows, z2.columns,
                           z2.bcd_version, z2.buttons, z2.surface_width,
                           z2.surface_height };
    unsigned nb = s5l_mt_frame_encode(&geom, &c, 1u, 7u, 123u, b);
    CHECK(na == nb && !memcmp(a, b, na), "MTZ2 shared frame encoding");

    s5l_mtz1_power_pin(&d, false);
    CHECK(!d.firmware && !d.atn, "power off resets");
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
