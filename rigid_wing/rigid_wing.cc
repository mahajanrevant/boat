#include "rigid_wing.h"

#include <sys/socket.h>
#include <arpa/inet.h>

#include "control/util.h"
#include "gflags.h"

DEFINE_int32(rigid_wing_port, 3333, "Port to talk to rigid wing on");

namespace sailbot {

RigidWing::RigidWing()
    : Node(0.1), feedback_queue_("rigid_wing_feedback", true),
      feedback_msg_(AllocateMessage<msg::RigidWingFeedback>()), sock_fd_(-1),
      conn_fd_(-1) {
  InitSocket();
  RegisterHandler<msg::RigidWingCmd>(
      "rigid_wing_cmd",
      [this](const msg::RigidWingCmd &cmd) { ConstructAndSend(cmd); });
}

namespace {
constexpr int kBufLen = 13;
}

void RigidWing::Iterate() {
  if (conn_fd_ == -1) {
    while (!AcceptConnection()) {
      if (util::IsShutdown()) {
        return;
      }
    }
  }

  // Now, attempt to read:
  char buf[16];
  int n = -1;
  while (true) {
    n = read(conn_fd_, buf, 16);
    if (n > 0) {
      // Got data, we're good!
      break;
    } else if (n == 0) {
      // EOF--socket is closed
      conn_fd_ = -1;
      return;
    } else {
      // Error, probably just no data available
      PLOG(INFO) << "Bad read--probably just no data available";
    }
    WaitForChange(conn_fd_, {1/*sec*/, 0/*usec*/});
    if (util::IsShutdown()) {
      return;
    }
  }

  buf[kBufLen] = 0;
  ParseMessage(buf, n, feedback_msg_);
  feedback_queue_.send(feedback_msg_);
}

void RigidWing::ConstructAndSend(const msg::RigidWingCmd &cmd) {
  char buf[16];
  int n = ConstructMessage(cmd, buf, 16);
  // Check that we have a connection...
  if (conn_fd_ != -1) {
    write(conn_fd_, buf, n);
  }
}

int RigidWing::ConstructMessage(const msg::RigidWingCmd &cmd, char *buf, size_t buf_len) {
  int heel = util::ToDeg(cmd.heel());
  int max_heel = util::ToDeg(cmd.max_heel());
  int aoa = cmd.servo_pos();
  return snprintf(buf, buf_len, "[%01d %03d %02d %03d]", (int)cmd.state(), heel,
                  max_heel, aoa);
}

int RigidWing::WaitForChange(int fd, struct timeval tv) {
  // Do timeout based select
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);
  return select(fd + 1, &read_fds, nullptr, nullptr, &tv);
}

namespace {
int atoi(const char *a, size_t len) {
  int retval = 0;
  for (size_t i = 0; i < len; ++i) {
    retval *= 10;
    retval += *(a++) - 0x30;
  }
  return retval;
}

}  // namespace

void RigidWing::ParseMessage(const char *buf, size_t buf_len,
                             msg::RigidWingFeedback *feedback) {
  feedback->clear_angle_of_attack();
  feedback->clear_servo_pos();
  feedback->clear_battery();
  if (buf_len != kBufLen) {
    LOG(WARNING) << "Buffer must be exactly 13 bytes long";
    return;
  }
  if (buf[0] != '[') {
    LOG(WARNING) << "Invalid received message (bad first char)";
    return;
  }

  feedback->set_angle_of_attack(
      util::norm_angle(util::ToRad((float)atoi(buf + 1, 3))));
  // buf[4] = ' '
  feedback->set_servo_pos(atoi(buf + 5, 3));
  // buf[8] = ' '
  feedback->set_battery((float)atoi(buf + 9, 3) / 1000.);

  if (buf[kBufLen-1] != ']') {
    LOG(WARNING) << "Message not terminated properly";
  }
}

void RigidWing::InitSocket() {
  sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  // Prevent "address already in use" problem.
  int option = 1;
  setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
  // Set non-blocking I/O
  int flags = fcntl(sock_fd_, F_GETFL, 0);
  PLOG_IF(FATAL, flags == -1) << "Failed to receive flags";
  flags |= O_NONBLOCK;
  PLOG_IF(FATAL, fcntl(sock_fd_, F_SETFL, flags) == -1)
      << "Failed to set fnctl flags";


  struct sockaddr_in serv_addr;
  // Zero out serv_addr
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(FLAGS_rigid_wing_port);
  PCHECK(::bind(sock_fd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0)
      << "Bind failed";
  PCHECK(listen(sock_fd_, 1) == 0) << "Listen failed";
}

bool RigidWing::AcceptConnection() {
  int result = WaitForChange(sock_fd_, {1 /*sec*/, 0 /*usec*/});
  if (result == 0) {
    VLOG(1) << "Looking for connections timed out...";
  } else {
    // Creates non-blocking socket.
    // Ignore address information
    int fd = accept4(sock_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    PLOG_IF(WARNING, fd == -1) << "Error on accept";
    conn_fd_ = fd;
    return true;
  }
  return false;
}

}  // namespace sailbot
