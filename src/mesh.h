#include <cglm/cglm.h>
#include "types.h"

typedef struct {
  vec3 pos;
  vec3 normal;
  vec3 uv;
} Vertex;


void generate_cube(Vertex* vertices, u16* indices);
