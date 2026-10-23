#pragma once
#include <stdint.h>

// Local ARM64 little-endian protocol. One sealed snapshot FD per frame, then ACK.
// This bootstrap transport intentionally copies wl_shm pixels; it is not DMA-BUF.
#define PRWL_MAGIC 0x4c575250u
#define PRWL_MAX_DIM 4096u
#define PRWL_MAX_BYTES (64u * 1024u * 1024u)
struct prwl_frame {
    uint32_t magic, version, width, height, stride, format, bytes, sequence;
};
