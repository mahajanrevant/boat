#include "movable.h"

int main(int argc, char *argv[]) {
  sailbot::util::SetCurrentThreadRealtimePriority(10);
  sailbot::util::Init(argc, argv);

  sailbot::control::Movable movable;
  movable.Run();
}
