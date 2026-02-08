#include "print.h"
#include "vulkan/vulkan_core.h"
void print(const char* fmt, ...) {
  char    buf[2048];
  va_list args;
  va_start(args, fmt);
  stbsp_vsprintf(buf, fmt, args);
  va_end(args);
  fputs(buf, stdout);
}
