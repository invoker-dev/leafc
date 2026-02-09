#include "mesh.h"

// NOTE: AI!
void generate_cube(Vertex* vertices, u16* indices) {
  // 6 face normals (Front, Back, Up, Down, Right, Left)
  float normals[6][3] = {
      { 0,  0,  1},
      { 0,  0, -1},
      { 0,  1,  0},
      { 0, -1,  0},
      { 1,  0,  0},
      {-1,  0,  0}
  };

  // Helper to find the "right" and "up" vectors for each face
  for (int f = 0; f < 6; f++) {
    float* n = normals[f];
    // Create two vectors (u, v) perpendicular to the normal to find the corners
    float u[3] = {n[1], n[2], n[0]};
    float v[3]; // Cross product n x u
    v[0] = n[1] * u[2] - n[2] * u[1];
    v[1] = n[2] * u[0] - n[0] * u[2];
    v[2] = n[0] * u[1] - n[1] * u[0];

    // 4 Corners per face
    float corner_offsets[4][2] = {
        {-1, -1},
        { 1, -1},
        { 1,  1},
        {-1,  1}
    };
    float uv_coords[4][2] = {
        {0, 0},
        {1, 0},
        {1, 1},
        {0, 1}
    };

    for (int i = 0; i < 4; i++) {
      int v_idx = f * 4 + i;
      // Position: Center + (u * offset_x) + (v * offset_y)
      vertices[v_idx].pos[0] =
          n[0] + u[0] * corner_offsets[i][0] + v[0] * corner_offsets[i][1];
      vertices[v_idx].pos[1] =
          n[1] + u[1] * corner_offsets[i][0] + v[1] * corner_offsets[i][1];
      vertices[v_idx].pos[2] =
          n[2] + u[2] * corner_offsets[i][0] + v[2] * corner_offsets[i][1];

      // Normal: Same for all 4 vertices of the face
      vertices[v_idx].normal[0] = n[0];
      vertices[v_idx].normal[1] = n[1];
      vertices[v_idx].normal[2] = n[2];

      // UVs
      vertices[v_idx].uv[0] = uv_coords[i][0];
      vertices[v_idx].uv[1] = uv_coords[i][1];
    }

    // Indices (same logic as before)
    int i_idx          = f * 6;
    int v_off          = f * 4;
    u16 face_indices[] = {0, 1, 2, 2, 3, 0};
    for (int j = 0; j < 6; j++)
      indices[i_idx + j] = v_off + face_indices[j];
  }
}
