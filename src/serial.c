#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#include "config.h"
#include "serial-channel.h"
#include "uds-channel.h"

const char default_portname[] = "/dev/ttyUSB0";

void signal_handler(int sig) {
  fprintf(stderr, "SIGNAL %d caught\n", sig);
  switch (sig) {
    case SIGINT:
      fprintf(stderr, "SIGINT received\n");
      break;
  }
}

static void init(int argc, char *argv[]) {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  if(sigprocmask(SIG_BLOCK, &sigset, NULL) < 0 ) {
      error_message("error: could not block SIGINT: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }
  fprintf(stderr, "signal handler SIGINT is now blocked\n");

  const char *p = default_portname;
  if (argc > 1 && strstr(argv[1], "/dev/") != NULL) {
    p = argv[1];
  }
  if (!serialChannel.Open(p)) {
    error_message("error: could not create serial communication channel\n");
    exit(EXIT_FAILURE);
  }

  if (!udsChannel.Open()) {
    error_message("error: could not create UDS communication channel\n");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char* argv[]) {
  fd_set fds;

  init(argc, argv);
  for (;;) {
    //struct timeval tv = { .tv_sec = 30, .tv_usec = 0, };

    FD_ZERO(&fds);
    FD_SET(udsChannel.sock, &fds);
    FD_SET(serialChannel.sock, &fds);
    int max = (serialChannel.sock < udsChannel.sock) ? udsChannel.sock
                                                     : serialChannel.sock;
    int active_socks = select(max + 1, &fds, NULL, NULL, NULL);
    switch (active_socks) {
    case 0:  // timeout
      fprintf(stderr, "error: select() returned with timeout\n");
      exit(EXIT_FAILURE);
      break;
    case -1: // error
      fprintf(stderr, "error: select() failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
      break;
    default: // number of sockets is rc
      for (int i = 0; i < active_socks; ++i) {
        if (FD_ISSET(udsChannel.sock, &fds)) {
          udsMessage msg;
          if (udsChannel.Read(&msg)) {

              switch (msg.typ) {
                  case udsmsg_control:
                      fprintf(stderr, "control message received, so terminate now\n");
                      goto loop1;
                      break;
                  case udsmsg_host2serial:
                  {
                    // write payload to serial channel
                    ssize_t n = serialChannel.Write(msg.payload, msg.len);
                    assert(n > 0 && "write n>0 bytes to serial channel");
#if 0
                    fprintf(stderr, "->");
                    for (int i = 0; i < msg.len; ++i) {
                      switch (msg.payload[i]) {
                      case '\t':
                        fprintf(stderr, "\\t");
                        break;
                      case '\r':
                        fprintf(stderr, "\\r");
                        break;
                      case '\n':
                        fprintf(stderr, "\\n");
                        break;
                      default:
                        fprintf(stderr, "%c", msg.payload[i]);
                      }
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                  }
                  default:
                      assert(0 && "not expected message type");
              }




          }
        } else if (FD_ISSET(serialChannel.sock, &fds)) {
          unsigned char buf[4096];
          ssize_t n = serialChannel.Read(buf, sizeof buf);
          switch (n) {
          case -1:
            error_message("error: read() failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
            break;
          case 0:
            error_message("read() received zero/EOF\n");
            goto loop1;
            break;
          default:
            buf[n] = '\0';
            udsChannel.Write(udsmsg_serial2host, (char *)buf);
#if 0
            // dump message to console
            fprintf(stderr, "<-");
            for (int i = 0; i < n; ++i) {
              switch (buf[i]) {
              case '\t':
                fprintf(stderr, "\\t");
                break;
              case '\r':
                fprintf(stderr, "\\r");
                break;
              case '\n':
                fprintf(stderr, "\\n");
                break;
              default:
                fprintf(stderr, "%c", buf[i]);
              }
            }
            fprintf(stderr, "\n");
#endif
            break;
          }
        }
      }
    }
  }
loop1:
 
  return 0;
}

