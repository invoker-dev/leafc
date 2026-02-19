#include <cglm/cglm.h>
#include "types.h"

typedef struct {
  vec3 pos;
  vec3 normal;
  vec3 uv;
} Vertex;


void genCube(Vertex* vertices, u16* indices);
