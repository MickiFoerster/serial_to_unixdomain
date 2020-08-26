#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "uds-channel.h"

#define UNIX_DOMAIN_SOCKET_PATH "/tmp/uds-server.uds" // note max size of UNIX_PATH_MAX

static bool uds_open_channel(void);
static void uds_close_channel(void);
static bool uds_read_channel(udsMessage *msg);
static void uds_write_channel(udsMessageType typ, const char *str);

udsChannelType udsChannel = {
    .sock = -1,
    .Open = uds_open_channel,
    .Close = uds_close_channel,
    .Read = uds_read_channel,
    .Write = uds_write_channel,
};

static bool uds_read_channel(udsMessage *msg) {
  if (!msg)
    return false;
  // read type first
  size_t toread = sizeof msg->typ;
  for (; toread > 0;) {
    ssize_t n = read(udsChannel.sock, &msg->typ, toread);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        continue;
      }
      error_message("error: read() failed: %s\n", strerror(errno));
      return false;
    } else if (n == 0) {
      return false;
    }
    toread -= n;
  }

  // read length of message
  toread = sizeof msg->len;
  memset(&msg->len, 1, sizeof msg->len);
  for (; toread > 0;) {
    ssize_t n =
        read(udsChannel.sock, &msg->len + (sizeof msg->len - toread), toread);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        continue;
      }
      error_message("error: read() failed: %s\n", strerror(errno));
      return false;
    } else if (n == 0) {
      return false;
    }
    toread -= n;
  }

  // read payload
  msg->payload = malloc(sizeof(msg->payload[0]) * (msg->len + 1));
  assert(msg->payload);
  toread = msg->len;
  unsigned char *p = msg->payload;
  for (; toread > 0;) {
    ssize_t n = read(udsChannel.sock, p, toread);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        continue;
      }
      error_message("error: read() failed: %s\n", strerror(errno));
      return false;
    } else if (n == 0) {
      return false;
    }
    p += n;
    toread -= n;
  }
  assert(p - msg->payload == msg->len);
  *p = '\0';

  return true;
}

static void uds_write_channel(udsMessageType typ, const char *str) {
  udsMessage msg = {
      .typ = typ,
      .len = strlen(str),
      .payload = (unsigned char *)str,
  };
  ssize_t towrite = sizeof msg.typ + sizeof msg.len + msg.len;
  unsigned char *buf = malloc(sizeof(unsigned char) * towrite);
  assert(buf);
  unsigned char *p = &buf[0];
  memcpy(p, &msg.typ, sizeof msg.typ);
  p += sizeof msg.typ;
  memcpy(p, &msg.len, sizeof msg.len);
  p += sizeof msg.len;
  memcpy(p, &msg.payload[0], msg.len);
  p += msg.len;
  size_t msg_size = p - buf;
  assert(towrite == msg_size);

  for (; towrite > 0;) {
    ssize_t n = write(udsChannel.sock, buf, towrite);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        continue;
      }
      error_message("error: could not write to channel: %s\n", strerror(errno));
      break; // exit loop
    }
    towrite -= n;
  }
}

static bool uds_open_channel(void) {
  int sock;
  int rc;
  struct sockaddr_un addr;

  sock = socket(PF_LOCAL, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket() failed");
    return false;
  }

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_LOCAL;
  strncpy(addr.sun_path, UNIX_DOMAIN_SOCKET_PATH, sizeof addr.sun_path);

  rc = connect(sock, (struct sockaddr *)&addr, sizeof addr);
  if (rc < 0) {
    perror("connect() failed");
    close(sock);
    return false;
  }

  udsChannel.sock = sock;
  atexit(udsChannel.Close);
  return true;
}

static void uds_close_channel(void) {
  for (;;) {
    int rc = close(udsChannel.sock);
    if (rc == -1) {
      switch (errno) {
      case EINTR:
        continue;
      }
      error_message("error: could not close channel properly: %s\n",
                    strerror(errno));
    }
    udsChannel.sock = -1;
    break; // exit loop
  }
  fprintf(stderr, "info: UDS channel closed\n");
  fflush(stderr);
}
