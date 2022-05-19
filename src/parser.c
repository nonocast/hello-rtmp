#include <assert.h>
#include <librtmp/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char byte;

#define FLV_CODEC_ID_AVC (7)
#define AVC_SEQUENCE_HEADER (0)
#define AVC_NALU (1)

enum tag_types { TAGTYPE_AUDIODATA = 8, TAGTYPE_VIDEODATA = 9, TAGTYPE_SCRIPTDATAOBJECT = 18 };
const char *flv_tag_types[] = {"", "", "", "", "", "", "", "", "audio", "video", "", "", "", "", "", "", "", "", "script data"};
const char *frame_types[] = {"not defined by standard",
                             "keyframe (for AVC, a seekable frame)",
                             "inter frame (for AVC, a non-seekable frame)",
                             "disposable inter frame (H.263 only)",
                             "generated keyframe (reserved for server use only)",
                             "video info/command frame"};

const char *codec_ids[] = {"not defined by standard", "JPEG (currently unused)", "Sorenson H.263", "Screen video", "On2 VP6", "On2 VP6 with alpha channel", "Screen video version 2", "AVC"};

const char *avc_packet_types[] = {"AVC sequence header", "AVC NALU", "AVC end of sequence (lower level NALU sequence ender is not required or supported)"};

typedef struct flv_header {
  uint8_t signature[3];
  uint8_t version;
  uint8_t type_flags;
  uint32_t data_offset;
} __attribute__((__packed__)) __;

typedef struct flv_header flv_header_t;

typedef struct {
  struct flv_tag *next;
  size_t offset; // 文件偏移量

  uint8_t tag_type;
  uint32_t data_size;
  uint32_t timestamp;
  uint8_t timestamp_ext;
  uint32_t stream_id;
  void *data;
} flv_tag_t;

typedef struct {
  void *data;
} data_tag_t;

typedef struct {
  uint8_t frame_type;
  uint8_t codec_id;
  void *data;
} video_tag_t;

typedef struct {
  uint8_t avc_packet_type; // 0x00 - AVC sequence header, 0x01 - AVC NALU
  uint32_t composition_time;
  void *data;
} avc_video_packet_t;

typedef struct {
  uint8_t configurationVersion;
  uint8_t AVCProfileIndication;
  uint8_t profile_compatibility;
  uint8_t AVCLevelIndication;
  uint8_t lengthSizeMinusOne;
  uint8_t numOfSequenceParameterSets;
  uint16_t sequenceParameterSetLength;
  uint8_t numOfPictureParameterSets;
  uint16_t pictureParameterSetLength;
  void *pps;
  void *sps;
} avc_decoder_configuration_record_t;

typedef struct {
  uint32_t size; // tag->data_size - 9
  void *data; // nalu(s): nalu1-len nalu1 data nalu2-len nalu2 ... naluN-len naluN
} avc_nalus_t;

static FILE *infile = NULL;
static flv_header_t flv_header;
static flv_tag_t *flv_tag_list_head;
static flv_tag_t *flv_tag_list_current;
static uint32_t flv_tag_list_size;

void die(char *);
void generate_h264_file();

void flv_read_header();
flv_tag_t *flv_read_tag();
video_tag_t *read_video_tag(flv_tag_t *flv_tag);
data_tag_t *read_data_tag(flv_tag_t *);

void push_tag(flv_tag_t *);
size_t get_tag_count();
size_t get_video_tag_count();

size_t fread_UI8(uint8_t *, FILE *file);
size_t fread_UI16(uint16_t *, FILE *file);
size_t fread_UI24(uint32_t *, FILE *file);
size_t fread_UI32(uint32_t *, FILE *file);
uint8_t flv_get_bits(uint8_t, uint8_t, uint8_t);

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
  printf("flv video tag count: %lu\n", get_video_tag_count());

  // video tags
  int i = 0;
  flv_tag_t *current = flv_tag_list_head;
  if (current != NULL) {
    do {
      if (TAGTYPE_SCRIPTDATAOBJECT == current->tag_type) {
        byte *amf_buffer = ((data_tag_t *) current->data)->data;
        size_t amf_len = current->data_size;

        RTMP_Log(RTMP_LOGINFO, "%s, t: %d, offset: 0x%08lx, data size: %d", flv_tag_types[current->tag_type], current->timestamp, current->offset, current->data_size);
        RTMP_LogHexString(RTMP_LOGINFO, amf_buffer, amf_len);

      } else if (TAGTYPE_VIDEODATA == current->tag_type) {
        ++i;
        RTMP_Log(RTMP_LOGDEBUG, "%s, t: %d, offset: 0x%08lx, data size: %d", flv_tag_types[current->tag_type], current->timestamp, current->offset, current->data_size);
      }
    } while ((current = (flv_tag_t *) current->next) != NULL && i < 5);
  }

  // mv video tag to h264 file
  // generate_h264_file();

  RTMP_Log(RTMP_LOGDEBUG, "the end.");
  release();

  return 0;
}

void generate_h264_file() {
  // open outfile
  FILE *outfile = fopen("out.h264", "wb");
  static const byte startcode[] = {0x00, 0x00, 0x00, 0x01};

  // write pps/sps
  flv_tag_t *current = flv_tag_list_head;
  assert(current);

  do {
    if (TAGTYPE_VIDEODATA == current->tag_type) {
      video_tag_t *video_tag = (video_tag_t *) current->data;
      avc_video_packet_t *packet = (avc_video_packet_t *) video_tag->data;
      if (AVC_SEQUENCE_HEADER == packet->avc_packet_type) {
        avc_decoder_configuration_record_t *record = (avc_decoder_configuration_record_t *) packet->data;

        RTMP_LogHex(RTMP_LOGINFO, record->sps, record->sequenceParameterSetLength);
        fwrite(&startcode, sizeof(startcode), 1, outfile);
        fwrite(record->sps, record->sequenceParameterSetLength, 1, outfile);

        RTMP_LogHex(RTMP_LOGINFO, record->pps, record->pictureParameterSetLength);
        fwrite(&startcode, sizeof(startcode), 1, outfile);
        fwrite(record->pps, record->pictureParameterSetLength, 1, outfile);
      } else if (AVC_NALU == packet->avc_packet_type) {
        avc_nalus_t *nalus = (avc_nalus_t *) packet->data;

        // AVCC to AnnexB
        uint32_t offset = 0;
        uint32_t nalu_len = 0;
        while (offset < (nalus->size - 4)) {
          memcpy(&nalu_len, nalus->data + offset, 4);
          nalu_len = ntohl(nalu_len);
          printf(".");
          fwrite(&startcode, sizeof(startcode), 1, outfile);
          fwrite(nalus->data + offset + 4, nalu_len, 1, outfile);

          offset += nalu_len + 4;
        }
      }
    }
  } while ((current = (flv_tag_t *) current->next) != NULL);
  printf("\n");

  fclose(outfile);
}

void die(char *message) {
  RTMP_Log(RTMP_LOGERROR, "error: %s", message);
  exit(-1);
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
  // flv header size
  static size_t offset = 9;

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

  tag->offset = offset + 4;
  offset += 15 + tag->data_size;

  switch (tag->tag_type) {
  case TAGTYPE_SCRIPTDATAOBJECT:
    tag->data = (void *) read_data_tag(tag);
    break;

  case TAGTYPE_AUDIODATA:
    tag->data = malloc((size_t) tag->data_size);
    if (tag->data_size != fread(tag->data, 1, (size_t) tag->data_size, infile)) {
      return NULL;
    }
    break;

  case TAGTYPE_VIDEODATA:
    tag->data = (void *) read_video_tag(tag);
    break;
  default:
    die("unknown tag type");
    break;
  }

  return tag;
}

data_tag_t *read_data_tag(flv_tag_t *flv_tag) {
  // TODO: READ AMF0

  data_tag_t *tag = NULL;
  tag = malloc(sizeof(data_tag_t));

  tag->data = malloc((size_t) flv_tag->data_size);
  if (flv_tag->data_size != fread(tag->data, 1, (size_t) flv_tag->data_size, infile)) {
    return NULL;
  }

  RTMP_LogHexString(RTMP_LOGDEBUG2, tag->data, flv_tag->data_size);

  return tag;
}

video_tag_t *read_video_tag(flv_tag_t *flv_tag) {
  video_tag_t *tag = NULL;
  uint8_t head = 0;

  tag = malloc(sizeof(video_tag_t));

  if (1 != fread_UI8(&head, infile)) return NULL;

  tag->frame_type = flv_get_bits(head, 4, 4);
  tag->codec_id = flv_get_bits(head, 0, 4);

  RTMP_Log(RTMP_LOGDEBUG, "  Video tag:");
  RTMP_Log(RTMP_LOGDEBUG, "    Frame type: %u - %s", tag->frame_type, frame_types[tag->frame_type]);
  RTMP_Log(RTMP_LOGDEBUG, "    Codec ID: %u - %s", tag->codec_id, codec_ids[tag->codec_id]);

  if (tag->codec_id != FLV_CODEC_ID_AVC) {
    tag->data = malloc((size_t) flv_tag->data_size - 1);
    fread(tag->data, 1, (size_t) flv_tag->data_size - 1, infile);
    return tag;
  }

  // AVC: h.264 nalu
  avc_video_packet_t *packet = NULL;
  packet = malloc(sizeof(avc_video_packet_t));
  if (1 != fread_UI8(&(packet->avc_packet_type), infile)) return NULL;
  if (3 != fread_UI24(&(packet->composition_time), infile)) return NULL;

  RTMP_Log(RTMP_LOGDEBUG, "    AVC video packet:");
  RTMP_Log(RTMP_LOGDEBUG, "      AVC packet type: %u - %s", packet->avc_packet_type, avc_packet_types[packet->avc_packet_type]);
  RTMP_Log(RTMP_LOGDEBUG, "      AVC composition time: %i", packet->composition_time);

  if (AVC_SEQUENCE_HEADER == packet->avc_packet_type) {
    // AVCDecoderConfigurationRecord支持多组SPS/PPS
    // 这里只考虑了一组的情况
    // len: 30
    // packet->data = malloc((size_t) flv_tag->data_size - 5);
    // fread(packet->data, 1, (size_t) flv_tag->data_size - 5, infile);

    avc_decoder_configuration_record_t *record = NULL;
    record = malloc(sizeof(avc_decoder_configuration_record_t));

    // ISO_14496_15
    RTMP_Log(RTMP_LOGDEBUG, "      AVCDecoderCOnfigurationRecord:");
    if (1 != fread_UI8(&(record->configurationVersion), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        Configuration Version: %d", record->configurationVersion);

    if (1 != fread_UI8(&(record->AVCProfileIndication), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        AVC Profile Indeication: %d", record->AVCProfileIndication);

    if (1 != fread_UI8(&(record->profile_compatibility), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        Profile Compatibility: %d", record->profile_compatibility);

    if (1 != fread_UI8(&(record->AVCLevelIndication), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        AVC Level Indication: %d", record->AVCLevelIndication);

    if (1 != fread_UI8(&(record->lengthSizeMinusOne), infile)) return NULL;
    record->lengthSizeMinusOne = record->lengthSizeMinusOne & 0x03; // & 0000 0011
    RTMP_Log(RTMP_LOGDEBUG, "        Minus One: %d", record->lengthSizeMinusOne);

    if (1 != fread_UI8(&(record->numOfSequenceParameterSets), infile)) return NULL;
    record->numOfSequenceParameterSets = record->numOfSequenceParameterSets & 0x1f; // & 0001 1111
    assert(1 == record->numOfSequenceParameterSets);
    RTMP_Log(RTMP_LOGDEBUG, "        SPS num: %d", record->numOfSequenceParameterSets);

    if (2 != fread_UI16(&(record->sequenceParameterSetLength), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        SPS length: %d", record->sequenceParameterSetLength);

    record->sps = malloc(record->sequenceParameterSetLength);
    if (record->sequenceParameterSetLength != fread(record->sps, 1, record->sequenceParameterSetLength, infile)) return NULL;
    RTMP_LogHex(RTMP_LOGDEBUG, record->sps, record->sequenceParameterSetLength);

    if (1 != fread_UI8(&(record->numOfPictureParameterSets), infile)) return NULL;
    assert(1 == record->numOfPictureParameterSets);
    RTMP_Log(RTMP_LOGDEBUG, "        PPS num: %d", record->numOfPictureParameterSets);

    if (2 != fread_UI16(&(record->pictureParameterSetLength), infile)) return NULL;
    RTMP_Log(RTMP_LOGDEBUG, "        PPS length: %d", record->pictureParameterSetLength);

    record->pps = malloc(record->pictureParameterSetLength);
    if (record->pictureParameterSetLength != fread(record->pps, 1, record->pictureParameterSetLength, infile)) return NULL;
    RTMP_LogHex(RTMP_LOGDEBUG, record->pps, record->pictureParameterSetLength);

    packet->data = record;
  } else if (AVC_NALU == packet->avc_packet_type) {
    avc_nalus_t *nalus = NULL;
    nalus = malloc(sizeof(avc_nalus_t));
    nalus->size = flv_tag->data_size - 5;
    nalus->data = malloc((size_t) nalus->size);
    fread(nalus->data, 1, (size_t) nalus->size, infile);
    packet->data = nalus;
  } else {
    packet->data = malloc((size_t) flv_tag->data_size - 5);
    fread(packet->data, 1, (size_t) flv_tag->data_size - 5, infile);
  }

  tag->data = packet;
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

size_t get_video_tag_count() {
  size_t size = 0;
  flv_tag_t *current = flv_tag_list_head;
  if (current != NULL) {
    do {
      if (TAGTYPE_VIDEODATA == current->tag_type) ++size;
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

size_t fread_UI16(uint16_t *ptr, FILE *file) {
  assert(NULL != ptr);
  size_t count = 0;
  uint8_t bytes[2] = {0};
  *ptr = 0;
  count = fread(bytes, 2, 1, file);
  *ptr = (bytes[0] << 8) | bytes[1];
  return count * 2;
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

/*
 * @brief read bits from 1 byte
 * @param[in] value: 1 byte to analysize
 * @param[in] start_bit: start from the low bit side
 * @param[in] count: number of bits
 */
uint8_t flv_get_bits(uint8_t value, uint8_t start_bit, uint8_t count) {
  uint8_t mask = 0;

  mask = (uint8_t) (((1 << count) - 1) << start_bit);
  return (mask & value) >> start_bit;
}
