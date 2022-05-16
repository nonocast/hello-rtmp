#include <librtmp/rtmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define APP_SUCCESS 0
#define APP_FAILED 1

// rtmpdump -r rtmp://media3.scctv.net/live/scctv_800 -o test.flv
static void usage();
static void parse_args(int, char **, char **, char **);
static void sigIntHandler(int);

int main(int argc, char *argv[]) {
  // data
  FILE *file = 0;
  char *output = "out.flv";
  char *url;
  RTMP rtmp = {0};

  // parse options and arguments
  parse_args(argc, argv, &url, &output);
  printf("rtmp url: %s\n", url);
  printf("output: %s\n", output);
  if (!(file = fopen(output, "wb"))) {
    fprintf(stderr, "Open file FAILED\n");
    fclose(file);
    exit(APP_FAILED);
  }

  signal(SIGINT, sigIntHandler);

  RTMP_Init(&rtmp);

  if (!RTMP_SetupURL(&rtmp, url)) {
    fprintf(stderr, "RTMP SetupURL FAILED\n");
    return APP_FAILED;
  }

  if (!RTMP_Connect(&rtmp, NULL)) {
    fprintf(stderr, "RTMP Connect FAILED\n");
    RTMP_Close(&rtmp);
    return APP_FAILED;
  }

  if (!RTMP_ConnectStream(&rtmp, 0)) {
    fprintf(stderr, "RTMP ConnectStream FAILED\n");
    RTMP_Close(&rtmp);
    return APP_FAILED;
  }

  int size = 2 * 1024 * 1024; // 2M bytes
  char buffer[size];
  int count;
  int total;

  while (!RTMP_ctrlC && (count = RTMP_Read(&rtmp, buffer, size)) > 0) {
    if (fwrite(buffer, sizeof(char), count, file) != count) {
      fprintf(stderr, "RTMP read FAILED\n");
      break;
    }
    total += count;
    printf("Receive: %5d Byte, Total: %5.2f kB\n", count, total * 1.0 / 1024);
  }

  printf("# EOF");
  fclose(file);
  RTMP_Close(&rtmp);

  return APP_SUCCESS;
}

static void usage() {
  printf("Usage: dump -o out.flv rtmp://media3.scctv.net/live/scctv_800\n");
}

static void parse_args(int argc, char *argv[], char **url, char **output) {
  int c;
  while ((c = getopt(argc, argv, "o:")) != -1) {
    switch (c) {
    case 'o':
      *output = optarg;
      break;
    default:
      usage();
      break;
    }
  }

  *url = argv[optind];
  if (*url == NULL) {
    usage();
    exit(APP_FAILED);
  }
}

static void sigIntHandler(int sig) {
  RTMP_ctrlC = TRUE;
  printf("  Caught signal: %d, cleaning up, just a second...\n", sig);
  signal(SIGINT, SIG_IGN);
}