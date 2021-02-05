#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <muduo/base/Logging.h>

#include "common/macros.h"

#include "log_util.h"

int g_pipes[2];
int g_message_size = 0;
int g_socket_type = -1;
int g_timeout_sec = 0;
pid_t g_chpid = -1;

volatile sig_atomic_t g_parent_timeout = 0;
volatile sig_atomic_t g_child_timeout = 0;

// set parent value
void ParentSigalarmHandler(int) { g_parent_timeout = 1; }

// set parent value
void ParentSigchldHandler(int) { g_child_timeout = 1; }

// set child value
void ChildSigalarmHandler(int) { g_child_timeout = 1; }

// void ParentXXXHandler(int) {
//  int saved_errno = errno;
//  ...
//  errno = saved_errno;
//}

void RunParent() {
  LOG_INFO << "parent pid: " << getpid();

  ::close(g_pipes[1]);

  signal(SIGPIPE, SIG_IGN);

  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = ParentSigalarmHandler;
  sigaction(SIGALRM, &sa, nullptr);

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = ParentSigchldHandler;
  sigaction(SIGCHLD, &sa, nullptr);

  unsigned int left_seconds = alarm(g_timeout_sec + 5);
  ASSERT_EXIT(left_seconds == 0);

#define TIMEOUT_BREAK(x)        \
  ({                            \
    auto result = (x);          \
    if (g_parent_timeout > 0) { \
      break;                    \
    }                           \
    if (g_child_timeout > 0) {  \
      ::alarm(0);               \
      break;                    \
    }                           \
    if (result < 0) {           \
      LOG_SYSERR << #x;         \
      break;                    \
    }                           \
    result;                     \
  })

  char buf[g_message_size];
  while (g_parent_timeout == 0 && g_child_timeout == 0) {
    ssize_t bytes_read = TIMEOUT_BREAK(::read(g_pipes[0], buf, sizeof(buf)));
    if (bytes_read > 0) {
      TIMEOUT_BREAK(::write(g_pipes[0], buf, bytes_read));
    }
  }

  // loop until parent timeout or child timeout
  while (g_child_timeout == 0) {
    if (g_parent_timeout > 0) {
      // unsafe
      ::kill(g_chpid, SIGTERM);
      LOG_WARN << "child process has run timeout and will be killed by signal "
                  "SIGTERM";
      break;
    }
    const struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000};
    ::nanosleep(&ts, nullptr);
  }

  ::wait(nullptr);

#undef TIMEOUT_BREAK
}

void RunChild() {
  LOG_INFO << "child pid: " << getpid();

  ::close(g_pipes[0]);

  signal(SIGPIPE, SIG_IGN);

  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = ChildSigalarmHandler;
  sigaction(SIGALRM, &sa, nullptr);

  unsigned int left_seconds = alarm(g_timeout_sec);
  ASSERT_EXIT(left_seconds == 0);

  char buf[g_message_size];
  for (int i = 0; i < g_message_size; ++i) {
    buf[i] = static_cast<char>(i % 128);
  }

#define TIMEOUT_BREAK(x)       \
  ({                           \
    auto result = (x);         \
    if (g_child_timeout > 0) { \
      break;                   \
    }                          \
    if (result < 0) {          \
      LOG_SYSERR << #x;        \
      break;                   \
    }                          \
    result;                    \
  })

  uint64_t total_bytes_read = 0;
  uint64_t total_messages_read = 0;
  size_t message_size = g_message_size;
  while (g_child_timeout == 0 && message_size > 0) {
    TIMEOUT_BREAK(::write(g_pipes[1], buf, message_size));
    ssize_t bytes_read = TIMEOUT_BREAK(::read(g_pipes[1], buf, sizeof(buf)));
    total_bytes_read += bytes_read;
    ++total_messages_read;
    message_size = bytes_read;
  }

  LOG_INFO << total_bytes_read << " total bytes read";
  LOG_INFO << total_messages_read << " total messages read";

  if (total_messages_read > 0) {
    LOG_INFO << static_cast<double>(total_bytes_read) /
                    static_cast<double>(total_messages_read)
             << " average message size";
  }

  if (g_timeout_sec > 0) {
    LOG_INFO << static_cast<double>(total_bytes_read) /
                    (g_timeout_sec * 1024 * 1024)
             << " MiB/s throughput";
  }
#undef TIMEOUT_BREAK
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    fprintf(stderr,
            "Usage: %s <message_size> <socket_type, 1:stream, 2:dgram> "
            "<timeout_sec>\n",
            argv[0]);
    return 0;
  }

  g_message_size = atoi(argv[1]);
  g_socket_type = atoi(argv[2]);
  g_timeout_sec = atoi(argv[3]);
  ASSERT_EXIT(g_message_size > 0 && g_message_size <= 32 * 1024);
  ASSERT_EXIT(g_socket_type >= 1 && g_socket_type <= 2);
  ASSERT_EXIT(g_timeout_sec > 0 && g_timeout_sec <= 3600);

  int socket_type = (g_socket_type == 1) ? SOCK_STREAM : SOCK_DGRAM;
  int result = HANDLE_EINTR(
      ::socketpair(AF_UNIX, socket_type | SOCK_CLOEXEC, 0, g_pipes));
  ASSERT_EXIT(result == 0);

  g_chpid = fork();
  ASSERT_EXIT(g_chpid >= 0);

  if (g_chpid > 0) {
    RunParent();
  } else {
    RunChild();
  }

  return 0;
}
