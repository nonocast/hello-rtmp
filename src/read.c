#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

#define bswap_24(data) ((((data) >> 24) & 0x000000FF) | (((data) >> 8) & 0x0000FF00) | (((data) << 8) & 0x00FF0000) | (((data) << 24) & 0xFF000000))

#define APP_SUCCESS 0
#define APP_FAILED 1

typedef unsigned char byte;

// #pragma pack(1)
// equal: __attribute__((__packed__))
struct flv_header {
  byte signature[3];
  byte version;
  byte flags;
  uint32_t offset;
} __attribute__((__packed__)); // make it 9 bytes

struct tag_header {
  byte type;
  byte size[3];
  byte timestamp[3];
  uint32_t reserved;
};

static void usage();
static void parse_args(int, char **, char **);
static void read_flv_header(struct flv_header *, FILE *);
static void read_previous_tag_size(FILE *);
static void read_tag_header(struct tag_header *, FILE *);
static char *tag_name(byte);

int main(int argc, char *argv[]) {
  printf("%lu\n", sizeof(struct flv_header));

  return 0;
  FILE *file = 0;
  char *input;

  parse_args(argc, argv, &input);
  if (!(file = fopen(input, "rb"))) {
    fprintf(stderr, "Open file FAILED\n");
    fclose(file);
    exit(APP_FAILED);
  }

  struct flv_header flv_header;
  read_flv_header(&flv_header, file);

  do {
    read_previous_tag_size(file);

    struct tag_header tag_header;
    read_tag_header(&tag_header, file);
    break;
  } while (true);

  fclose(file);
  return APP_SUCCESS;
}

static void read_flv_header(struct flv_header *flv_header, FILE *file) {
  // https://www.zhihu.com/question/39875579
  fread(flv_header, sizeof(struct flv_header), 1, file);
  printf("flv header:\n");
  printf("  .signature: %c%c%c\n", flv_header->signature[0], flv_header->signature[1], flv_header->signature[2]);
  printf("  .version: %d\n", flv_header->version);
  printf("  .flags: %x\n", flv_header->flags);
  printf("    .TypeFlagsVideo: %s\n", flv_header->flags & 0x01 ? "true" : "false"); // 0000 0001
  printf("    .TypeFlagsAudio: %s\n", flv_header->flags & 0x04 ? "true" : "false"); // 0000 0100

  printf("  .offset: %d\n", bswap_32(flv_header->offset));
  printf("-----------------------------\n");
  fseek(file, bswap_32(flv_header->offset), SEEK_SET);
}

static void read_previous_tag_size(FILE *file) {
  // ingore PreviousTagSize
  getw(file);
}

static void read_tag_header(struct tag_header *tag_header, FILE *file) {
  fread(tag_header, sizeof(struct tag_header), 1, file);
  printf("flv tag:\n");
  printf("  .type: %s\n", tag_name(tag_header->type));
  int data_size = tag_header->size[0];
  printf("  .data size: %d\n", data_size);
}

static char *tag_name(byte type) {
  char *tag;
  switch (type) {
  case 8:
    tag = "audio";
    break;
  case 9:
    tag = "video";
    break;
  case 18:
    tag = "script";
    break;
  default:
    tag = "unknown";
    break;
  }
  return tag;
}

static void usage() {
  printf("Usage: read out.flv\n");
}

static void parse_args(int argc, char *argv[], char **input) {
  *input = argv[1];
  if (*input == NULL) {
    usage();
    exit(APP_FAILED);
  }
}
