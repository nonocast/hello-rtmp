#include <assert.h>
#include <librtmp/amf.h>
#include <librtmp/log.h>
#include <librtmp/rtmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char byte;
enum tag_types { TAGTYPE_AUDIODATA = 8, TAGTYPE_VIDEODATA = 9, TAGTYPE_SCRIPTDATAOBJECT = 18 };
const char *flv_tag_types[] = {"", "", "", "", "", "", "", "", "audio", "video", "", "", "", "", "", "", "", "", "script data"};

typedef struct flv_tag {
  byte type;
  byte head[11];
  uint32_t offset;
  size_t size;
  uint32_t data_offset;
  size_t data_size;
} flv_tag_t;

void open_flv();
void open_rtmp();
void close_rtmp();
void send_metadata();
void die();
void get_metadata_tag();
void get_video_tags();
flv_tag_t *read_tag();
void read_ui24(uint32_t *, void *);
static int DumpMetaData(AMFObject *);
static void sigIntHandler(int sig);

// variable
FILE *infile;
RTMP *rtmp;
flv_tag_t **video_tags;
size_t video_tag_size;
flv_tag_t *metadata_tag;

int main(int argc, char *argv[]) {
  open_flv();
  open_rtmp();

  send_metadata();

  byte buffer[1024 * 1024 * 20];
  flv_tag_t *current;

  int i = 0;
  while (!RTMP_ctrlC) {
    current = video_tags[i];
    fseek(infile, current->offset, SEEK_SET);
    fread(buffer, 1, current->size, infile);
    int count = RTMP_Write(rtmp, (char *) buffer, current->size);
    RTMP_Log(RTMP_LOGINFO, "send video tag: %d", count);

    usleep(20 * 1000);

    if (i + 1 == video_tag_size) {
      i = 0;
      RTMP_Log(RTMP_LOGINFO, "************************");
    } else {
      ++i;
    }
  }

  close_rtmp();

  return 0;
}

void send_metadata() {
  byte buffer[1024];
  fseek(infile, metadata_tag->offset, SEEK_SET);
  fread(buffer, 1, metadata_tag->size, infile);
  int count = RTMP_Write(rtmp, (char *) buffer, metadata_tag->size);

  RTMP_Log(RTMP_LOGINFO, "send metadata: %d", count);
}

void open_rtmp() {
  char *url = "rtmp://live.nonocast.cn/app/nonocast";

  rtmp = RTMP_Alloc();
  RTMP_Init(rtmp);
  if (RTMP_SetupURL(rtmp, url) < 0) {
    RTMP_Log(RTMP_LOGERROR, "RTMP_SetupURL FAILED: %s", url);
    die();
  }

  RTMP_EnableWrite(rtmp);

  if (RTMP_Connect(rtmp, NULL) < 0) {
    RTMP_Log(RTMP_LOGERROR, "Connect FAILED: %s", url);
    die();
  }

  if (RTMP_ConnectStream(rtmp, 0) < 0) {
    RTMP_Log(RTMP_LOGERROR, "ConnectStream FAILED");
    die();
  }

  signal(SIGINT, sigIntHandler);
}

void close_rtmp() {
  RTMP_Close(rtmp);
  RTMP_Free(rtmp);
  rtmp = NULL;
}

void open_flv() {
  RTMP_LogSetLevel(RTMP_LOGINFO);
  // RTMP_LogSetLevel(RTMP_LOGALL);
  // infile = fopen("out.flv", "rb");
  infile = fopen("/Users/nonocast/Desktop/clip.flv", "rb");
  get_metadata_tag();
  get_video_tags();

  // check
  flv_tag_t *tag = video_tags[0];
  RTMP_Log(RTMP_LOGDEBUG, "%s", flv_tag_types[tag->type]);
  RTMP_LogHex(RTMP_LOGDEBUG, tag->head, sizeof(tag->head));
  RTMP_Log(RTMP_LOGDEBUG, "  tag offset: 0x%08x, tag size: %lu", tag->offset, tag->size);
  RTMP_Log(RTMP_LOGDEBUG, "  data offset: 0x%08x, data size: %lu", tag->data_offset, tag->data_size);

  RTMP_Log(RTMP_LOGINFO, "video tags: %lu", video_tag_size);
}

void die() {
  fclose(infile);
  exit(1);
}

void get_metadata_tag() {
  flv_tag_t *tag = malloc(sizeof(flv_tag_t));
  tag->offset = 13; // 9: flv_header 4: prev
  fseek(infile, tag->offset, SEEK_SET);
  fread(tag->head, 1, 11, infile);
  tag->type = *tag->head;

  read_ui24((uint32_t *) &(tag->data_size), tag->head + 1);
  tag->data_offset = tag->offset + 11;
  tag->size = tag->data_size + 11;

  RTMP_Log(RTMP_LOGDEBUG, "%s", flv_tag_types[tag->type]);
  RTMP_LogHex(RTMP_LOGDEBUG, tag->head, sizeof(tag->head));
  RTMP_Log(RTMP_LOGDEBUG, "  tag offset: 0x%08x, tag size: %lu", tag->offset, tag->size);
  RTMP_Log(RTMP_LOGDEBUG, "  data offset: 0x%08x, data size: %lu", tag->data_offset, tag->data_size);

  char *obj_buffer = malloc(tag->data_size);
  fread(obj_buffer, 1, tag->data_size, infile);

  AMFObject obj;
  AMF_Decode(&obj, obj_buffer, tag->data_size, false);
  RTMP_Log(RTMP_LOGDEBUG, "  ---------------------------------------");
  DumpMetaData(&obj);

  metadata_tag = tag;
}

void get_video_tags() {
  video_tags = malloc(sizeof(flv_tag_t *) * 4096);
  flv_tag_t *current;

  // move to first tag address
  fseek(infile, 13, SEEK_SET);
  while ((current = read_tag()) != NULL) {
    if (TAGTYPE_VIDEODATA == current->type) {
      video_tags[video_tag_size++] = current;
    }
  }
}

flv_tag_t *read_tag() {
  static uint32_t _offset = 13;

  flv_tag_t *tag = malloc(sizeof(flv_tag_t));
  tag->offset = _offset;

  if (11 != fread(tag->head, 1, 11, infile)) return NULL;
  tag->type = *tag->head;

  read_ui24((uint32_t *) &(tag->data_size), tag->head + 1);
  tag->data_offset = tag->offset + 11;
  tag->size = tag->data_size + 11;

  RTMP_Log(RTMP_LOGDEBUG, "%s", flv_tag_types[tag->type]);
  RTMP_LogHex(RTMP_LOGDEBUG, tag->head, sizeof(tag->head));
  RTMP_Log(RTMP_LOGDEBUG, "  tag offset: 0x%08x, tag size: %lu", tag->offset, tag->size);
  RTMP_Log(RTMP_LOGDEBUG, "  data offset: 0x%08x, data size: %lu", tag->data_offset, tag->data_size);

  // update _offset
  _offset += 11 + tag->data_size + 4;

  if (0 != fseek(infile, tag->data_size + 4, SEEK_CUR)) return NULL;

  return tag;
}

void read_ui24(uint32_t *out, void *ptr) {
  uint8_t *bytes = (uint8_t *) ptr;
  *out = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}

// from amf.c
static int DumpMetaData(AMFObject *obj) {
  AMFObjectProperty *prop;
  int n, len;
  for (n = 0; n < obj->o_num; n++) {
    char str[256] = "";
    prop = AMF_GetProp(obj, NULL, n);
    switch (prop->p_type) {
    case AMF_OBJECT:
    case AMF_ECMA_ARRAY:
    case AMF_STRICT_ARRAY:
      if (prop->p_name.av_len) RTMP_Log(RTMP_LOGINFO, "%.*s:", prop->p_name.av_len, prop->p_name.av_val);
      DumpMetaData(&prop->p_vu.p_object);
      break;
    case AMF_NUMBER:
      snprintf(str, 255, "%.2f", prop->p_vu.p_number);
      break;
    case AMF_BOOLEAN:
      snprintf(str, 255, "%s", prop->p_vu.p_number != 0. ? "TRUE" : "FALSE");
      break;
    case AMF_STRING:
      len = snprintf(str, 255, "%.*s", prop->p_vu.p_aval.av_len, prop->p_vu.p_aval.av_val);
      if (len >= 1 && str[len - 1] == '\n') str[len - 1] = '\0';
      break;
    case AMF_DATE:
      snprintf(str, 255, "timestamp:%.2f", prop->p_vu.p_number);
      break;
    default:
      snprintf(str, 255, "INVALID TYPE 0x%02x", (unsigned char) prop->p_type);
    }
    if (str[0] && prop->p_name.av_len) {
      RTMP_Log(RTMP_LOGINFO, "  %-22.*s%s", prop->p_name.av_len, prop->p_name.av_val, str);
    }
  }
  return FALSE;
}

static void sigIntHandler(int sig) {
  RTMP_ctrlC = TRUE;
  // RTMP_LogPrintf("Caught signal: %d, cleaning up, just a second...\n", sig);
  // ignore all these signals now and let the connection close
  signal(SIGINT, SIG_IGN);
}
