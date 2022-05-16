#include <assert.h>
#include <librtmp/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char byte;

enum tag_types { TAGTYPE_AUDIODATA = 8, TAGTYPE_VIDEODATA = 9, TAGTYPE_SCRIPTDATAOBJECT = 18 };
const char *flv_tag_types[] = {"", "", "", "", "", "", "", "", "audio", "video", "", "", "", "", "", "", "", "", "script data"};

typedef struct flv_header {
  uint8_t signature[3];
  uint8_t version;
  uint8_t type_flags;
  uint32_t data_offset;
} __attribute__((__packed__)) __;

typedef struct flv_header flv_header_t;

typedef struct {
  struct flv_tag *next;
  uint8_t tag_type;
  uint32_t data_size;
  uint32_t timestamp;
  uint8_t timestamp_ext;
  uint32_t stream_id;
  void *data;
} flv_tag_t;

typedef struct {
  uint8_t frame_type;
  uint8_t codec_id;
  void *data;
} video_tag_t;

static FILE *infile = NULL;
static flv_header_t flv_header;
static flv_tag_t *flv_tag_list_head;
static flv_tag_t *flv_tag_list_current;
static uint32_t flv_tag_list_size;

void flv_read_header();
flv_tag_t *flv_read_tag();

void push_tag(flv_tag_t *);
size_t get_tag_count();

size_t fread_UI8(uint8_t *, FILE *file);
size_t fread_UI24(uint32_t *, FILE *file);
size_t fread_UI32(uint32_t *, FILE *file);

void print_tag(flv_tag_t *);
void release();

void usage(char *program_name) {
  printf("Usage: %s [-v] infile\n", program_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  RTMP_LogSetLevel(RTMP_LOGINFO);

  char *prog = argv[0];
  int c;
  while ((c = getopt(argc, argv, "vV")) != -1) {
    switch (c) {
    case 'v':
      RTMP_LogSetLevel(RTMP_LOGDEBUG);
      break;
    case 'V':
      RTMP_LogSetLevel(RTMP_LOGDEBUG2);
      break;
    default:
      usage(prog);
      break;
    }
  }

  RTMP_Log(RTMP_LOGDEBUG, "input: %s", argv[1]);
  infile = fopen(argv[optind], "r");
  if (!infile) {
    usage(argv[0]);
  }

  flv_read_header();
  flv_tag_t *tag;

  while ((tag = flv_read_tag()) != NULL) {
    push_tag(tag);
    print_tag(tag);
    if (feof(infile)) break;
  }

  printf("flv tag count: %lu\n", get_tag_count());

  // video tags
  int i = 0;
  flv_tag_t *current = flv_tag_list_head;
  if (current != NULL) {
    do {
      if (TAGTYPE_VIDEODATA == current->tag_type) {
        ++i;
        RTMP_Log(RTMP_LOGINFO, "%s, t: %d, data size: %d", flv_tag_types[current->tag_type], current->timestamp, current->data_size);
      }
    } while ((current = (flv_tag_t *) current->next) != NULL && i < 10);
  }

  RTMP_Log(RTMP_LOGDEBUG, "the end.");
  release();

  return 0;
}

void release() {
  // release file
  fclose(infile);
  // TODO: release tag list and data
}

void flv_read_header() {
  fread(&flv_header, sizeof(flv_header_t), 1, infile);
  flv_header.data_offset = ntohl(flv_header.data_offset);
  RTMP_Log(RTMP_LOGDEBUG, "FLV file version: %u", flv_header.version);
  RTMP_Log(RTMP_LOGDEBUG, "  Contains audio tags: %s", flv_header.type_flags & (1 << 0) ? "Yes" : "No");
  RTMP_Log(RTMP_LOGDEBUG, "  Contains video tags: %s", flv_header.type_flags & (1 << 2) ? "Yes" : "No");
  RTMP_Log(RTMP_LOGDEBUG, "  Data offset: %d", flv_header.data_offset);
}

flv_tag_t *flv_read_tag() {
  static uint32_t i = 0;
  RTMP_Log(RTMP_LOGDEBUG2, "---------------------- flv_read_tag.begin: %d", ++i);

  size_t count = 0;
  uint32_t prev_tag_size = 0;
  flv_tag_t *tag = NULL;

  tag = malloc(sizeof(flv_tag_t));

  if (4 != fread_UI32(&prev_tag_size, infile)) return NULL;
  if (1 != fread_UI8(&(tag->tag_type), infile)) return NULL;
  if (3 != fread_UI24(&(tag->data_size), infile)) return NULL;
  if (3 != fread_UI24(&(tag->timestamp), infile)) return NULL;
  if (1 != fread_UI8(&(tag->timestamp_ext), infile)) return NULL;
  if (3 != fread_UI24(&(tag->stream_id), infile)) return NULL;

  RTMP_Log(RTMP_LOGDEBUG, "Tag type: %u - %s", tag->tag_type, flv_tag_types[tag->tag_type]);
  RTMP_Log(RTMP_LOGDEBUG, "  Data size: %d", tag->data_size);
  RTMP_Log(RTMP_LOGDEBUG, "  Timestamp: %d", tag->timestamp);
  RTMP_Log(RTMP_LOGDEBUG, "  Timestamp etxended: %d", tag->timestamp_ext);
  RTMP_Log(RTMP_LOGDEBUG, "  StreamID: %d", tag->stream_id);

  tag->data = malloc((size_t) tag->data_size);
  if (tag->data_size != fread(tag->data, 1, (size_t) tag->data_size, infile)) {
    return NULL;
  }

  return tag;
}

void push_tag(flv_tag_t *tag) {
  if (flv_tag_list_head == NULL) {
    flv_tag_list_head = tag;
    flv_tag_list_current = tag;
  }

  flv_tag_list_current->next = (struct flv_tag *) tag;
  flv_tag_list_current = tag;
}

void print_tag(flv_tag_t *tag) {}

/*
 * flv tag list operation
 */
size_t get_tag_count() {
  size_t size = 0;
  flv_tag_t *current = flv_tag_list_head;
  if (current != NULL) {
    do {
      ++size;
    } while ((current = (flv_tag_t *) current->next) != NULL);
  }
  return size;
}

/*
 * convert from BE (FLV) to LE
 */
size_t fread_UI8(uint8_t *ptr, FILE *file) {
  assert(NULL != ptr);
  return fread(ptr, 1, 1, file);
}

size_t fread_UI24(uint32_t *ptr, FILE *file) {
  assert(NULL != ptr);
  size_t count = 0;
  uint8_t bytes[3] = {0};
  *ptr = 0;
  count = fread(bytes, 3, 1, file);
  *ptr = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
  return count * 3;
}

size_t fread_UI32(uint32_t *ptr, FILE *file) {
  assert(NULL != ptr);
  size_t count = 0;
  uint8_t bytes[4] = {0};
  *ptr = 0;
  count = fread(bytes, 4, 1, file);
  *ptr = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
  return count * 4;
}