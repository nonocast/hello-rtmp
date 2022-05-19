#include <arpa/inet.h>
#include <librtmp/log.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef unsigned char byte;

static int sockfd = 0;

void print_addr(struct hostent *, struct sockaddr_in *);
void random_bytes(byte *buffer, size_t size);
void die() { exit(1); }

int main() {
  RTMP_LogSetLevel(RTMP_LOGALL);

  struct hostent *host;
  struct sockaddr_in server;

  const char hostname[] = "live.nonocast.cn";
  if ((host = gethostbyname(hostname)) == NULL) {
    RTMP_Log(RTMP_LOGERROR, "gethostbyname FAILED");
    die();
  }
  memcpy(&server.sin_addr, host->h_addr_list[0], host->h_length);
  server.sin_family = AF_INET;
  server.sin_port = htons(1935);

  print_addr(host, &server);

  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    RTMP_Log(RTMP_LOGERROR, "create socket FAILED");
    die();
  }

  if (connect(sockfd, (struct sockaddr *) &server, sizeof(server)) == -1) {
    RTMP_Log(RTMP_LOGERROR, "connect FAILED");
    die();
  }

  RTMP_Log(RTMP_LOGDEBUG, "connected OK");

  byte c0[1] = {0x03};
  send(sockfd, c0, 1, 0);
  RTMP_Log(RTMP_LOGDEBUG, "send c0: %lu", sizeof(c0));
  RTMP_LogHex(RTMP_LOGDEBUG, c0, 1);

  byte c1[1536] = {0x00};
  random_bytes(c1, 1536);
  send(sockfd, c1, sizeof(c1), 0);
  RTMP_Log(RTMP_LOGDEBUG, "send c1: %lu", sizeof(c1));
  RTMP_LogHex(RTMP_LOGDEBUG, c1, 16);

  byte recv_buf[2046];
  int count;
  count = recv(sockfd, recv_buf, 1, 0);
  RTMP_Log(RTMP_LOGDEBUG, "recv s0: %d", count);
  RTMP_LogHex(RTMP_LOGDEBUG, recv_buf, count);

  count = recv(sockfd, recv_buf, 1536, 0);
  RTMP_Log(RTMP_LOGDEBUG, "recv s1: %d", count);
  RTMP_LogHex(RTMP_LOGDEBUG, recv_buf, 16);

  count = send(sockfd, recv_buf, 1536, 0);
  RTMP_Log(RTMP_LOGDEBUG, "send c2: %d", 1536);
  RTMP_LogHex(RTMP_LOGDEBUG, recv_buf, 16);

  count = recv(sockfd, recv_buf, 1536, 0);
  RTMP_Log(RTMP_LOGDEBUG, "recv s2: %d", 1536);
  RTMP_LogHex(RTMP_LOGDEBUG, recv_buf, 16);

  close(sockfd);

  return 0;
}

void print_addr(struct hostent *host, struct sockaddr_in *addr) {
  char buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer));
  RTMP_Log(RTMP_LOGDEBUG, "%s", host->h_name);
  RTMP_Log(RTMP_LOGDEBUG, "%s", buffer);
}

void random_bytes(byte *buffer, size_t size) {
  for (int i = 0; i < size; ++i) {
    buffer[i] = rand() % 0xff;
  }
}