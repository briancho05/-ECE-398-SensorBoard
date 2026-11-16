/**
 * dxl_protocol.h - Dynamixel Protocol 2.0 framing: CRC, byte stuffing,
 * streaming parser and status-packet builder. Pure C, no hardware access.
 */
#ifndef DXL_PROTOCOL_H
#define DXL_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define DXL_BROADCAST_ID        0xFEu
#define DXL_MAX_ID              0xFCu   /* 252 */

/* Instructions */
#define DXL_INST_PING           0x01u
#define DXL_INST_READ           0x02u
#define DXL_INST_WRITE          0x03u
#define DXL_INST_REG_WRITE      0x04u
#define DXL_INST_ACTION         0x05u
#define DXL_INST_FACTORY_RESET  0x06u
#define DXL_INST_REBOOT         0x08u
#define DXL_INST_CLEAR          0x10u
#define DXL_INST_STATUS         0x55u
#define DXL_INST_SYNC_READ      0x82u
#define DXL_INST_SYNC_WRITE     0x83u
#define DXL_INST_FAST_SYNC_READ 0x8Au
#define DXL_INST_BULK_READ      0x92u
#define DXL_INST_BULK_WRITE     0x93u

/* Status packet error codes */
#define DXL_ERR_NONE            0x00u
#define DXL_ERR_RESULT_FAIL     0x01u
#define DXL_ERR_INSTRUCTION     0x02u
#define DXL_ERR_CRC             0x03u
#define DXL_ERR_DATA_RANGE      0x04u
#define DXL_ERR_DATA_LENGTH     0x05u
#define DXL_ERR_DATA_LIMIT      0x06u
#define DXL_ERR_ACCESS          0x07u

/* Raw packet byte offsets */
#define DXL_OFS_ID              4u
#define DXL_OFS_LEN_L           5u
#define DXL_OFS_LEN_H           6u
#define DXL_OFS_INST            7u
#define DXL_OFS_PARAM           8u

#define DXL_MAX_PACKET          256u    /* largest raw (stuffed) packet assembled */
#define DXL_MAX_PARAMS          (DXL_MAX_PACKET - 10u)

typedef struct {
    uint8_t  id;
    uint8_t  inst;
    uint16_t nparams;
    uint8_t  params[DXL_MAX_PARAMS];    /* already de-stuffed */
} dxl_packet_t;

typedef struct {
    uint8_t  buf[DXL_MAX_PACKET];       /* raw packet as received */
    uint16_t idx;                       /* bytes stored; 0 = searching header */
    uint16_t total;                     /* full raw length once LEN is known */
    uint32_t shift;                     /* last 4 bytes, for header sync */
    bool     done;                      /* buf holds a complete packet */
} dxl_parser_t;

uint16_t dxl_crc16(uint16_t crc, const uint8_t *data, size_t len);

void dxl_parser_reset(dxl_parser_t *p);

/** Feed one byte. Returns true when p->buf holds a complete raw packet
 *  (valid until the next call). CRC is NOT checked here so that packets
 *  for other devices cost almost nothing. */
bool dxl_parser_feed(dxl_parser_t *p, uint8_t b);

/** True while a packet is partially assembled. */
static inline bool dxl_parser_busy(const dxl_parser_t *p) { return p->idx != 0u && !p->done; }

/** ID byte of the completed raw packet. */
static inline uint8_t dxl_parser_id(const dxl_parser_t *p) { return p->buf[DXL_OFS_ID]; }

/** Check CRC and de-stuff the completed packet into out.
 *  Returns false on CRC mismatch (out->id / out->inst are still filled). */
bool dxl_decode(const dxl_parser_t *p, dxl_packet_t *out);

/** Build a status packet (instruction 0x55) with byte stuffing and CRC.
 *  Returns the number of bytes written, or 0 if out_max is too small. */
size_t dxl_build_status(uint8_t id, uint8_t err, const uint8_t *data, size_t n,
                        uint8_t *out, size_t out_max);

#endif /* DXL_PROTOCOL_H */
