#include <librtmp/amf.h>
#include <librtmp/log.h>
#include <librtmp/rtmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char byte;

static int DumpMetaData(AMFObject *obj);
void encode1();
void encode2();
void decode();

int main() {
  RTMP_LogSetLevel(RTMP_LOGALL);

  // decode();
  encode2();

  return 0;
}

void encode2() {
  char pbuf[256], *pend = pbuf + sizeof(pbuf);
  char *enc = pbuf;

  memset(pbuf, 0, sizeof(pbuf));

  AVal str_foo = AVC("foo");
  AVal str_bar = AVC("bar");
  AVal str_fullscreen = AVC("fullscreen");

  *enc++ = AMF_OBJECT;

  enc = AMF_EncodeNamedString(enc, pend, &str_foo, &str_bar);
  enc = AMF_EncodeNamedBoolean(enc, pend, &str_fullscreen, true);

  *enc++ = 0;
  *enc++ = 0;
  *enc++ = AMF_OBJECT_END;

  RTMP_LogHexString(RTMP_LOGINFO, (unsigned char *) pbuf, enc - pbuf);
}

void encode1() {
  char pbuf[256], *pend = pbuf + sizeof(pbuf);
  char *enc = pbuf;

  memset(pbuf, 0, sizeof(pbuf));

  AMFObject obj;

  AVal str_foo = AVC("foo");
  AVal str_bar = AVC("bar");
  AVal str_fullscreen = AVC("fullscreen");

  AMFObjectProperty prop1;
  prop1.p_type = AMF_STRING;
  prop1.p_name = str_foo;
  prop1.p_vu.p_aval = str_bar;

  AMFObjectProperty prop2;
  prop2.p_type = AMF_BOOLEAN;
  prop2.p_name = str_fullscreen;
  prop2.p_vu.p_number = true;

  AMFObjectProperty props[] = {prop1, prop2};
  obj.o_num = 2;
  obj.o_props = props;

  enc = AMF_Encode(&obj, enc, pend);

  RTMP_LogHexString(RTMP_LOGINFO, (unsigned char *) pbuf, enc - pbuf);
}

void decode() {
  uint32_t offset = 0x0d + 11;
  size_t size = 573;
  byte buffer[size];

  FILE *file = fopen("out.flv", "r");
  memset(buffer, 0, size);
  fseek(file, offset, 0);
  fread(buffer, 1, size, file);
  RTMP_LogHexString(RTMP_LOGINFO, buffer, size);
  fclose(file);

  // parse amf
  AMFObject obj;
  AMF_Decode(&obj, (char *) buffer, size, FALSE);

  // AMF_Dump(&obj);

  DumpMetaData(&obj);
}

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